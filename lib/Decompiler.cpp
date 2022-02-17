/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/Decompiler.h"

#include <clang/Basic/TargetInfo.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/PassManager.h>
#include <llvm/InitializePasses.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/Transforms/Utils/Local.h>
#include <llvm/Transforms/Utils/LowerSwitch.h>

#include <memory>

#include "rellic/AST/CondBasedRefine.h"
#include "rellic/AST/DeadStmtElim.h"
#include "rellic/AST/DebugInfoCollector.h"
#include "rellic/AST/ExprCombine.h"
#include "rellic/AST/GenerateAST.h"
#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/LocalDeclRenamer.h"
#include "rellic/AST/LoopRefine.h"
#include "rellic/AST/NestedCondProp.h"
#include "rellic/AST/NestedScopeCombine.h"
#include "rellic/AST/NormalizeCond.h"
#include "rellic/AST/ReachBasedRefine.h"
#include "rellic/AST/StructFieldRenamer.h"
#include "rellic/AST/Z3CondSimplify.h"
#include "rellic/BC/Util.h"
#include "rellic/Exception.h"

namespace {

static void CloneMetadataInto(
    llvm::Instruction* dst,
    const llvm::SmallVector<std::pair<unsigned, llvm::MDNode*>, 16u>& mds) {
  for (auto [id, node] : mds) {
    switch (id) {
      case llvm::LLVMContext::MD_tbaa:
      case llvm::LLVMContext::MD_tbaa_struct:
      case llvm::LLVMContext::MD_noalias:
      case llvm::LLVMContext::MD_alias_scope:
        break;
      default:
        dst->setMetadata(id, node);
        break;
    }
  }
}

static void CopyMetadataTo(llvm::Value* src, llvm::Value* dst) {
  if (src == dst) {
    return;
  }
  llvm::Instruction *src_inst = llvm::dyn_cast_or_null<llvm::Instruction>(src),
                    *dst_inst = llvm::dyn_cast_or_null<llvm::Instruction>(dst);
  if (!src_inst || !dst_inst) {
    return;
  }

  llvm::SmallVector<std::pair<unsigned, llvm::MDNode*>, 16u> mds;
  src_inst->getAllMetadataOtherThanDebugLoc(mds);
  CloneMetadataInto(dst_inst, mds);
}

static void RemovePHINodes(llvm::Module& module) {
  std::vector<llvm::PHINode*> work_list;
  for (auto& func : module) {
    for (auto& inst : llvm::instructions(func)) {
      if (auto phi = llvm::dyn_cast<llvm::PHINode>(&inst)) {
        work_list.push_back(phi);
      }
    }
  }
  for (auto phi : work_list) {
    llvm::SmallVector<std::pair<unsigned, llvm::MDNode*>, 16u> mds;
    phi->getAllMetadataOtherThanDebugLoc(mds);
    auto new_alloca{DemotePHIToStack(phi)};
    CloneMetadataInto(new_alloca, mds);
  }
}

static void LowerSwitches(llvm::Module& module) {
  llvm::PassBuilder pb;
  llvm::ModulePassManager mpm;
  llvm::ModuleAnalysisManager mam;
  llvm::LoopAnalysisManager lam;
  llvm::CGSCCAnalysisManager cam;
  llvm::FunctionAnalysisManager fam;

  pb.registerFunctionAnalyses(fam);
  pb.registerModuleAnalyses(mam);
  pb.registerCGSCCAnalyses(cam);
  pb.registerLoopAnalyses(lam);

  pb.crossRegisterProxies(lam, fam, cam, mam);

  llvm::FunctionPassManager fpm;
  fpm.addPass(llvm::LowerSwitchPass());

  mpm.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(fpm)));
  mpm.run(module, mam);

  mam.clear();
  fam.clear();
  cam.clear();
  lam.clear();
}

static void InitOptPasses(void) {
  auto& pr{*llvm::PassRegistry::getPassRegistry()};
  initializeCore(pr);
  initializeAnalysis(pr);
}
};  // namespace

template <typename TKey, typename TValue>
static void CopyMap(const std::unordered_map<TKey*, TValue*>& from,
                    std::unordered_map<const TKey*, const TValue*>& to,
                    std::unordered_map<const TValue*, const TKey*>& inverse) {
  for (auto [key, value] : from) {
    if (value) {
      to[key] = value;
      inverse[value] = key;
    }
  }
}

template <typename TKey, typename TValue>
static void CopyMap(
    const std::unordered_multimap<TKey*, TValue*>& from,
    std::unordered_multimap<const TKey*, const TValue*>& to,
    std::unordered_multimap<const TValue*, const TKey*>& inverse) {
  for (auto [key, value] : from) {
    if (value) {
      to.insert({key, value});
      inverse.insert({value, key});
    }
  }
}

namespace rellic {
Result<DecompilationResult, DecompilationError> Decompile(
    std::unique_ptr<llvm::Module> module, DecompilationOptions options) {
  try {
    if (options.remove_phi_nodes) {
      RemovePHINodes(*module);
    }

    if (options.lower_switches) {
      LowerSwitches(*module);
    }

    InitOptPasses();
    rellic::DebugInfoCollector dic;
    dic.visit(*module);

    std::vector<std::string> args{"-Wno-pointer-to-int-cast", "-target",
                                  module->getTargetTriple()};
    auto ast_unit{clang::tooling::buildASTFromCodeWithArgs("", args, "out.c")};

    rellic::StmtToIRMap provenance;
    rellic::IRToTypeDeclMap type_decls;
    rellic::IRToValDeclMap value_decls;
    rellic::IRToStmtMap stmts;
    rellic::ArgToTempMap temp_decls;
    rellic::GenerateAST::run(*module, provenance, *ast_unit, type_decls,
                             value_decls, stmts, temp_decls);
    // TODO(surovic): Add llvm::Value* -> clang::Decl* map
    // Especially for llvm::Argument* and llvm::Function*.

    rellic::CompositeASTPass pass_ast(provenance, *ast_unit);
    auto& ast_passes{pass_ast.GetPasses()};

    if (options.dead_stmt_elimination) {
      ast_passes.push_back(
          std::make_unique<rellic::DeadStmtElim>(provenance, *ast_unit));
    }
    ast_passes.push_back(std::make_unique<rellic::LocalDeclRenamer>(
        provenance, *ast_unit, dic.GetIRToNameMap(), value_decls));
    ast_passes.push_back(std::make_unique<rellic::StructFieldRenamer>(
        provenance, *ast_unit, dic.GetIRTypeToDITypeMap(), type_decls));
    pass_ast.Run();

    rellic::CompositeASTPass pass_cbr(provenance, *ast_unit);
    auto& cbr_passes{pass_cbr.GetPasses()};

    if (options.condition_based_refinement.expression_normalize) {
      cbr_passes.push_back(
          std::make_unique<rellic::NormalizeCond>(provenance, *ast_unit));
    }
    if (!options.disable_z3) {
      auto zcs{std::make_unique<rellic::Z3CondSimplify>(provenance, *ast_unit)};
      // Simplifier to use during condition-based refinement
      z3::tactic tactic{zcs->GetZ3Context(), "skip"};
      for (auto name : options.condition_based_refinement.z3_tactics) {
        tactic = tactic & z3::tactic{zcs->GetZ3Context(), name.c_str()};
      }
      zcs->SetZ3Tactic(tactic);
      if (options.condition_based_refinement.z3_cond_simplify) {
        cbr_passes.push_back(std::move(zcs));
      }
      if (options.condition_based_refinement.nested_cond_propagate) {
        cbr_passes.push_back(
            std::make_unique<rellic::NestedCondProp>(provenance, *ast_unit));
      }
    }

    if (options.condition_based_refinement.nested_scope_combine) {
      cbr_passes.push_back(
          std::make_unique<rellic::NestedScopeCombine>(provenance, *ast_unit));
    }

    if (!options.disable_z3) {
      if (options.condition_based_refinement.cond_base_refine) {
        cbr_passes.push_back(
            std::make_unique<rellic::CondBasedRefine>(provenance, *ast_unit));
      }
      if (options.condition_based_refinement.reach_based_refine) {
        cbr_passes.push_back(
            std::make_unique<rellic::ReachBasedRefine>(provenance, *ast_unit));
      }
    }

    while (pass_cbr.Run()) {
      ;
    }

    rellic::CompositeASTPass pass_loop{provenance, *ast_unit};
    auto& loop_passes{pass_loop.GetPasses()};

    if (options.loop_refinement.loop_refine) {
      loop_passes.push_back(
          std::make_unique<rellic::LoopRefine>(provenance, *ast_unit));
    }
    if (options.loop_refinement.nested_scope_combine) {
      loop_passes.push_back(
          std::make_unique<rellic::NestedScopeCombine>(provenance, *ast_unit));
    }
    if (options.loop_refinement.expression_normalize) {
      loop_passes.push_back(
          std::make_unique<rellic::NormalizeCond>(provenance, *ast_unit));
    }
    while (pass_loop.Run()) {
      ;
    }

    rellic::CompositeASTPass pass_scope{provenance, *ast_unit};
    auto& scope_passes{pass_scope.GetPasses()};
    if (!options.disable_z3) {
      auto zcs{std::make_unique<rellic::Z3CondSimplify>(provenance, *ast_unit)};
      // Simplifier to use during condition-based refinement
      z3::tactic tactic{zcs->GetZ3Context(), "skip"};
      for (auto name : options.condition_based_refinement.z3_tactics) {
        tactic = tactic & z3::tactic{zcs->GetZ3Context(), name.c_str()};
      }
      zcs->SetZ3Tactic(tactic);
      if (options.condition_based_refinement.z3_cond_simplify) {
        scope_passes.push_back(std::move(zcs));
      }
      if (options.condition_based_refinement.nested_cond_propagate) {
        scope_passes.push_back(
            std::make_unique<rellic::NestedCondProp>(provenance, *ast_unit));
      }
    }

    if (options.scope_refinement.nested_scope_combine) {
      scope_passes.push_back(
          std::make_unique<rellic::NestedScopeCombine>(provenance, *ast_unit));
    }
    if (options.scope_refinement.expression_normalize) {
      scope_passes.push_back(
          std::make_unique<rellic::NormalizeCond>(provenance, *ast_unit));
    }
    while (pass_scope.Run()) {
      ;
    }

    rellic::CompositeASTPass pass_ec{provenance, *ast_unit};
    auto& ec_passes{pass_ec.GetPasses()};
    if (options.expression_combine) {
      ec_passes.push_back(
          std::make_unique<rellic::ExprCombine>(provenance, *ast_unit));
    }
    if (options.expression_normalize) {
      ec_passes.push_back(
          std::make_unique<rellic::NormalizeCond>(provenance, *ast_unit));
    }
    while (pass_ec.Run()) {
      ;
    }

    DecompilationResult result{};
    result.ast = std::move(ast_unit);
    result.module = std::move(module);
    CopyMap(provenance, result.stmt_provenance_map, result.value_to_stmt_map);
    CopyMap(value_decls, result.value_to_decl_map, result.decl_provenance_map);
    CopyMap(type_decls, result.type_to_decl_map, result.type_provenance_map);

    return Result<DecompilationResult, DecompilationError>(std::move(result));
  } catch (Exception& ex) {
    DecompilationError error{};
    error.message = ex.what();
    error.module = std::move(module);
    return Result<DecompilationResult, DecompilationError>(std::move(error));
  }
}
}  // namespace rellic