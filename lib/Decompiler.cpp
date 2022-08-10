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

static void InitOptPasses(void) {
  auto& pr{*llvm::PassRegistry::getPassRegistry()};
  initializeCore(pr);
  initializeAnalysis(pr);
}
}  // namespace

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

    ConvertArrayArguments(*module);
    RemoveInsertValues(*module);
    FindRedundantLoads(*module);

    InitOptPasses();
    rellic::DebugInfoCollector dic;
    dic.visit(*module);

    std::vector<std::string> args{"-Wno-pointer-to-int-cast",
                                  "-Wno-pointer-sign", "-target",
                                  module->getTargetTriple()};
    auto ast_unit{clang::tooling::buildASTFromCodeWithArgs("", args, "out.c")};

    rellic::Provenance provenance;
    rellic::GenerateAST::run(*module, provenance, *ast_unit);
    // TODO(surovic): Add llvm::Value* -> clang::Decl* map
    // Especially for llvm::Argument* and llvm::Function*.

    rellic::CompositeASTPass pass_ast(provenance, *ast_unit);
    auto& ast_passes{pass_ast.GetPasses()};

    if (options.dead_stmt_elimination) {
      ast_passes.push_back(
          std::make_unique<rellic::DeadStmtElim>(provenance, *ast_unit));
    }
    ast_passes.push_back(std::make_unique<rellic::LocalDeclRenamer>(
        provenance, *ast_unit, dic.GetIRToNameMap()));
    ast_passes.push_back(std::make_unique<rellic::StructFieldRenamer>(
        provenance, *ast_unit, dic.GetIRTypeToDITypeMap()));
    pass_ast.Run();

    rellic::CompositeASTPass pass_cbr(provenance, *ast_unit);
    auto& cbr_passes{pass_cbr.GetPasses()};

    if (options.condition_based_refinement.expression_normalize) {
      cbr_passes.push_back(
          std::make_unique<rellic::NormalizeCond>(provenance, *ast_unit));
    }
    if (!options.disable_z3) {
      auto zcs{std::make_unique<rellic::Z3CondSimplify>(provenance, *ast_unit)};
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
    if (options.loop_refinement.nested_cond_propagate) {
      loop_passes.push_back(
          std::make_unique<rellic::NestedCondProp>(provenance, *ast_unit));
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

    rellic::CompositeASTPass pass_final{provenance, *ast_unit};
    auto& final_passes{pass_final.GetPasses()};
    if (options.final_refinement.z3_cond_simplify) {
      final_passes.push_back(
          std::make_unique<rellic::Z3CondSimplify>(provenance, *ast_unit));
    }
    if (options.final_refinement.nested_cond_propagate) {
      final_passes.push_back(
          std::make_unique<rellic::NestedCondProp>(provenance, *ast_unit));
    }
    if (options.final_refinement.nested_scope_combine) {
      final_passes.push_back(
          std::make_unique<rellic::NestedScopeCombine>(provenance, *ast_unit));
    }
    while (pass_final.Run()) {
      ;
    }

    DecompilationResult result{};
    result.ast = std::move(ast_unit);
    result.module = std::move(module);
    CopyMap(provenance.stmt_provenance, result.stmt_provenance_map,
            result.value_to_stmt_map);
    CopyMap(provenance.value_decls, result.value_to_decl_map,
            result.decl_provenance_map);
    CopyMap(provenance.type_decls, result.type_to_decl_map,
            result.type_provenance_map);
    CopyMap(provenance.use_provenance, result.expr_use_map,
            result.use_expr_map);

    return Result<DecompilationResult, DecompilationError>(std::move(result));
  } catch (Exception& ex) {
    DecompilationError error{};
    error.message = ex.what();
    error.module = std::move(module);
    return Result<DecompilationResult, DecompilationError>(std::move(error));
  }
}
}  // namespace rellic