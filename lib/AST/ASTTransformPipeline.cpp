/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/ASTTransformPipeline.h"

#include <clang/AST/ASTContext.h>
#include <clang/Frontend/ASTUnit.h>

#include "rellic/AST/ASTPass.h"
#include "rellic/AST/CondBasedRefine.h"
#include "rellic/AST/DeadStmtElim.h"
#include "rellic/AST/DecompilationContext.h"
#include "rellic/AST/ExprCombine.h"
#include "rellic/AST/LoopRefine.h"
#include "rellic/AST/MaterializeConds.h"
#include "rellic/AST/NestedCondProp.h"
#include "rellic/AST/NestedScopeCombine.h"
#include "rellic/AST/ReachBasedRefine.h"
#include "rellic/AST/Z3CondSimplify.h"

namespace rellic {

ASTTransformPipeline::ASTTransformPipeline(
    clang::ASTUnit &ast_unit, std::unique_ptr<DecompilationContext> ctx)
    : ast_unit(ast_unit), dec_ctx(std::move(ctx)) {}

ASTTransformPipeline::~ASTTransformPipeline() = default;

Result<std::unique_ptr<ASTTransformPipeline>, std::string>
ASTTransformPipeline::Create(clang::ASTUnit &ast_unit) {
  // Create minimal decompilation context
  auto dec_ctx = DecompilationContext::CreateMinimal(ast_unit);

  if (!dec_ctx) {
    return Result<std::unique_ptr<ASTTransformPipeline>, std::string>(
        std::string("Failed to create decompilation context"));
  }

  return Result<std::unique_ptr<ASTTransformPipeline>, std::string>(
      std::unique_ptr<ASTTransformPipeline>(
          new ASTTransformPipeline(ast_unit, std::move(dec_ctx))));
}

clang::ASTContext &ASTTransformPipeline::GetAST() {
  return ast_unit.getASTContext();
}

void ASTTransformPipeline::EliminateDeadCode() {
  DeadStmtElim pass(*dec_ctx);
  pass.Run();
}

void ASTTransformPipeline::RefineControlFlow() {
  CompositeASTPass pipeline(*dec_ctx);
  auto &passes = pipeline.GetPasses();

  // Build the control flow refinement pipeline
  passes.push_back(std::make_unique<Z3CondSimplify>(*dec_ctx));
  passes.push_back(std::make_unique<NestedCondProp>(*dec_ctx));
  passes.push_back(std::make_unique<NestedScopeCombine>(*dec_ctx));
  passes.push_back(std::make_unique<CondBasedRefine>(*dec_ctx));
  passes.push_back(std::make_unique<ReachBasedRefine>(*dec_ctx));

  // Run to fixpoint
  pipeline.Fixpoint();
}

void ASTTransformPipeline::OptimizeLoops() {
  CompositeASTPass pipeline(*dec_ctx);
  auto &passes = pipeline.GetPasses();

  passes.push_back(std::make_unique<LoopRefine>(*dec_ctx));
  passes.push_back(std::make_unique<NestedCondProp>(*dec_ctx));
  passes.push_back(std::make_unique<NestedScopeCombine>(*dec_ctx));

  // Run to fixpoint
  pipeline.Fixpoint();
}

void ASTTransformPipeline::SimplifyExpressions() {
  // First materialize Z3 conditions
  {
    MaterializeConds materialize_pass(*dec_ctx);
    materialize_pass.Run();
  }

  // Then simplify expressions
  {
    ExprCombine combine_pass(*dec_ctx);
    combine_pass.Run();
  }

  // Final scope refinement
  {
    CompositeASTPass final_pipeline(*dec_ctx);
    auto &passes = final_pipeline.GetPasses();

    passes.push_back(std::make_unique<Z3CondSimplify>(*dec_ctx));
    passes.push_back(std::make_unique<NestedCondProp>(*dec_ctx));
    passes.push_back(std::make_unique<NestedScopeCombine>(*dec_ctx));

    final_pipeline.Fixpoint();
  }
}

void ASTTransformPipeline::RunAllPasses() {
  // Stage 1: Dead code elimination
  EliminateDeadCode();

  // Stage 2: Control flow refinement
  RefineControlFlow();

  // Stage 3: Loop optimization
  OptimizeLoops();

  // Stage 4: Expression simplification
  SimplifyExpressions();
}

}  // namespace rellic
