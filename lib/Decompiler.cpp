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
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/InitializePasses.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/Transforms/Utils/Local.h>

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
#include "rellic/AST/ReachBasedRefine.h"
#include "rellic/AST/StructFieldRenamer.h"
#include "rellic/AST/Z3CondSimplify.h"
#include "rellic/BC/Util.h"
#include "rellic/Exception.h"

using StmtToIRMap = std::unordered_map<clang::Stmt*, llvm::Value*>;

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
  llvm::legacy::PassManager pm;
  pm.add(llvm::createLowerSwitchPass());
  pm.run(module);
}

static void InitOptPasses(void) {
  auto& pr{*llvm::PassRegistry::getPassRegistry()};
  initializeCore(pr);
  initializeAnalysis(pr);
}

static void InitProvenanceMap(StmtToIRMap& provenance,
                              rellic::IRToStmtMap& init) {
  for (auto& item : init) {
    if (item.second) {
      provenance[item.second] = item.first;
    }
  }
}

static void UpdateProvenanceMap(StmtToIRMap& provenance,
                                rellic::StmtSubMap& substitutions) {
  for (auto& sub : substitutions) {
    auto it{provenance.find(sub.first)};
    if (it != provenance.end()) {
      provenance[sub.second] = it->second;
      provenance.erase(it);
    }
  }
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

    llvm::legacy::PassManager pm_ast;
    rellic::GenerateAST* gr{new rellic::GenerateAST(*ast_unit)};
    rellic::DeadStmtElim* dse{new rellic::DeadStmtElim(*ast_unit)};
    rellic::LocalDeclRenamer* ldr{new rellic::LocalDeclRenamer(
        *ast_unit, dic.GetIRToNameMap(), gr->GetIRToValDeclMap())};
    rellic::StructFieldRenamer* sfr{new rellic::StructFieldRenamer(
        *ast_unit, dic.GetIRTypeToDITypeMap(), gr->GetIRToTypeDeclMap())};
    pm_ast.add(gr);
    pm_ast.add(dse);
    pm_ast.add(ldr);
    pm_ast.add(sfr);
    pm_ast.run(*module);

    // TODO(surovic): Add llvm::Value* -> clang::Decl* map
    // Especially for llvm::Argument* and llvm::Function*.
    StmtToIRMap stmt_provenance;

    InitProvenanceMap(stmt_provenance, gr->GetIRToStmtMap());
    UpdateProvenanceMap(stmt_provenance, dse->GetStmtSubMap());

    rellic::Z3CondSimplify* zcs{new rellic::Z3CondSimplify(*ast_unit)};
    rellic::NestedCondProp* ncp{new rellic::NestedCondProp(*ast_unit)};
    rellic::NestedScopeCombine* nsc{new rellic::NestedScopeCombine(*ast_unit)};
    rellic::CondBasedRefine* cbr{new rellic::CondBasedRefine(*ast_unit)};
    rellic::ReachBasedRefine* rbr{new rellic::ReachBasedRefine(*ast_unit)};

    llvm::legacy::PassManager pm_cbr;
    if (!options.disable_z3) {
      // Simplifier to use during condition-based refinement
      zcs->SetZ3Simplifier(
          // Simplify boolean structure with AIGs
          z3::tactic(zcs->GetZ3Context(), "aig") &
          // Cheap local simplifier
          z3::tactic(zcs->GetZ3Context(), "simplify"));
      pm_cbr.add(zcs);
      pm_cbr.add(ncp);
    }

    pm_cbr.add(nsc);

    if (!options.disable_z3) {
      pm_cbr.add(cbr);
      pm_cbr.add(rbr);
    }

    while (pm_cbr.run(*module)) {
      UpdateProvenanceMap(stmt_provenance, zcs->GetStmtSubMap());
      UpdateProvenanceMap(stmt_provenance, ncp->GetStmtSubMap());
      UpdateProvenanceMap(stmt_provenance, nsc->GetStmtSubMap());
      UpdateProvenanceMap(stmt_provenance, cbr->GetStmtSubMap());
      UpdateProvenanceMap(stmt_provenance, rbr->GetStmtSubMap());
    }

    rellic::LoopRefine* lr{new rellic::LoopRefine(*ast_unit)};
    nsc = new rellic::NestedScopeCombine(*ast_unit);

    llvm::legacy::PassManager pm_loop;
    pm_loop.add(lr);
    pm_loop.add(nsc);
    while (pm_loop.run(*module)) {
      UpdateProvenanceMap(stmt_provenance, lr->GetStmtSubMap());
      UpdateProvenanceMap(stmt_provenance, nsc->GetStmtSubMap());
    }

    llvm::legacy::PassManager pm_scope;
    if (!options.disable_z3) {
      // Simplifier to use during final refinement
      zcs = new rellic::Z3CondSimplify(*ast_unit);
      ncp = new rellic::NestedCondProp(*ast_unit);
      zcs->SetZ3Simplifier(
          // Simplify boolean structure with AIGs
          z3::tactic(zcs->GetZ3Context(), "aig") &
          // Cheap simplification
          z3::tactic(zcs->GetZ3Context(), "simplify") &
          // Propagate bounds over bit-vectors
          z3::tactic(zcs->GetZ3Context(), "propagate-bv-bounds") &
          // Contextual simplification
          z3::tactic(zcs->GetZ3Context(), "ctx-simplify"));
      pm_scope.add(zcs);
      pm_scope.add(ncp);
    }

    nsc = new rellic::NestedScopeCombine(*ast_unit);

    pm_scope.add(nsc);
    while (pm_scope.run(*module)) {
      UpdateProvenanceMap(stmt_provenance, zcs->GetStmtSubMap());
      UpdateProvenanceMap(stmt_provenance, ncp->GetStmtSubMap());
      UpdateProvenanceMap(stmt_provenance, nsc->GetStmtSubMap());
    }

    llvm::legacy::PassManager pm_expr;
    rellic::ExprCombine* ec{new rellic::ExprCombine(*ast_unit)};
    pm_expr.add(ec);
    while (pm_expr.run(*module)) {
      UpdateProvenanceMap(stmt_provenance, ec->GetStmtSubMap());
    }

    DecompilationResult result{};
    result.ast = std::move(ast_unit);
    result.module = std::move(module);
    CopyMap(stmt_provenance, result.stmt_provenance_map,
            result.value_to_stmt_map);
    CopyMap(gr->GetIRToValDeclMap(), result.value_to_decl_map,
            result.decl_provenance_map);
    CopyMap(gr->GetIRToTypeDeclMap(), result.type_to_decl_map,
            result.type_provenance_map);

    return Result<DecompilationResult, DecompilationError>(std::move(result));
  } catch (Exception& ex) {
    DecompilationError error{};
    error.message = ex.what();
    error.module = std::move(module);
    return Result<DecompilationResult, DecompilationError>(std::move(error));
  }
}
}  // namespace rellic