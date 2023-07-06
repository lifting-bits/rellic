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
#include "rellic/AST/InlineReferences.h"
#include "rellic/AST/LocalDeclRenamer.h"
#include "rellic/AST/LoopRefine.h"
#include "rellic/AST/MaterializeConds.h"
#include "rellic/AST/NestedCondProp.h"
#include "rellic/AST/NestedScopeCombine.h"
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

    InitOptPasses();
    rellic::DebugInfoCollector dic;
    dic.visit(*module);

    std::vector<std::string> args{"-Wno-pointer-to-int-cast",
                                  "-Wno-pointer-sign", "-target",
                                  module->getTargetTriple()};
    // Silence clang warning
    // warning: unknown platform, assumming -mfloat-abi=soft
    const auto& triple{llvm::Triple(module->getTargetTriple())};
    if (triple.isARM()) {
        args.push_back("-mfloat-abi=soft");
    }
    auto ast_unit{clang::tooling::buildASTFromCodeWithArgs("", args, "out.c")};
    rellic::DecompilationContext dec_ctx(*ast_unit);

    for (auto& provider : options.additional_providers) {
      dec_ctx.type_provider->AddProvider(provider->create(dec_ctx));
    }

    rellic::GenerateAST::run(*module, dec_ctx);
    // TODO(surovic): Add llvm::Value* -> clang::Decl* map
    // Especially for llvm::Argument* and llvm::Function*.

    rellic::CompositeASTPass pass_ast(dec_ctx);
    auto& ast_passes{pass_ast.GetPasses()};

    ast_passes.push_back(std::make_unique<rellic::DeadStmtElim>(dec_ctx));
    ast_passes.push_back(std::make_unique<rellic::LocalDeclRenamer>(
        dec_ctx, dic.GetIRToNameMap()));
    ast_passes.push_back(std::make_unique<rellic::StructFieldRenamer>(
        dec_ctx, dic.GetIRTypeToDITypeMap()));
    pass_ast.Run();

    rellic::CompositeASTPass pass_cbr(dec_ctx);
    auto& cbr_passes{pass_cbr.GetPasses()};

    cbr_passes.push_back(std::make_unique<rellic::Z3CondSimplify>(dec_ctx));
    cbr_passes.push_back(std::make_unique<rellic::NestedCondProp>(dec_ctx));

    cbr_passes.push_back(std::make_unique<rellic::NestedScopeCombine>(dec_ctx));

    cbr_passes.push_back(std::make_unique<rellic::CondBasedRefine>(dec_ctx));
    cbr_passes.push_back(std::make_unique<rellic::ReachBasedRefine>(dec_ctx));
    cbr_passes.push_back(std::make_unique<rellic::InlineReferences>(dec_ctx));

    while (pass_cbr.Run()) {
      ;
    }

    rellic::CompositeASTPass pass_loop{dec_ctx};
    auto& loop_passes{pass_loop.GetPasses()};

    loop_passes.push_back(std::make_unique<rellic::LoopRefine>(dec_ctx));
    loop_passes.push_back(std::make_unique<rellic::NestedCondProp>(dec_ctx));
    loop_passes.push_back(
        std::make_unique<rellic::NestedScopeCombine>(dec_ctx));
    loop_passes.push_back(std::make_unique<rellic::InlineReferences>(dec_ctx));

    while (pass_loop.Run()) {
      ;
    }

    rellic::CompositeASTPass pass_scope{dec_ctx};
    auto& scope_passes{pass_scope.GetPasses()};
    scope_passes.push_back(std::make_unique<rellic::Z3CondSimplify>(dec_ctx));
    scope_passes.push_back(std::make_unique<rellic::NestedCondProp>(dec_ctx));

    scope_passes.push_back(
        std::make_unique<rellic::NestedScopeCombine>(dec_ctx));
    scope_passes.push_back(std::make_unique<rellic::InlineReferences>(dec_ctx));

    while (pass_scope.Run()) {
      ;
    }

    rellic::CompositeASTPass pass_ec{dec_ctx};
    auto& ec_passes{pass_ec.GetPasses()};
    ec_passes.push_back(std::make_unique<rellic::MaterializeConds>(dec_ctx));
    ec_passes.push_back(std::make_unique<rellic::ExprCombine>(dec_ctx));
    ec_passes.push_back(std::make_unique<rellic::InlineReferences>(dec_ctx));

    pass_ec.Run();

    DecompilationResult result{};
    result.ast = std::move(ast_unit);
    result.module = std::move(module);
    CopyMap(dec_ctx.stmt_provenance, result.stmt_provenance_map,
            result.value_to_stmt_map);
    CopyMap(dec_ctx.value_decls, result.value_to_decl_map,
            result.decl_provenance_map);
    CopyMap(dec_ctx.type_decls, result.type_to_decl_map,
            result.type_provenance_map);
    CopyMap(dec_ctx.use_provenance, result.expr_use_map, result.use_expr_map);

    return Result<DecompilationResult, DecompilationError>(std::move(result));
  } catch (Exception& ex) {
    DecompilationError error{};
    error.message = ex.what();
    error.module = std::move(module);
    return Result<DecompilationResult, DecompilationError>(std::move(error));
  }
}
}  // namespace rellic
