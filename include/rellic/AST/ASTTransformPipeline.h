/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <memory>
#include <string>

#include "rellic/Result.h"

namespace clang {
class ASTContext;
class ASTUnit;
}  // namespace clang

namespace rellic {

class DecompilationContext;

/// Pipeline for applying rellic's AST transformation passes to an existing AST.
/// Transforms the AST in-place through multiple optimization stages.
///
/// Usage example:
/// \code
///   auto pipeline_result = ASTTransformPipeline::Create(ast_unit);
///   if (pipeline_result.Succeeded()) {
///     auto pipeline = pipeline_result.TakeValue();
///     pipeline->RunAllPasses();
///     // AST is now optimized
///     pipeline->GetAST().getTranslationUnitDecl()->print(llvm::outs());
///   }
/// \endcode
class ASTTransformPipeline {
 public:
  /// Factory method to create a pipeline from an existing AST.
  /// Creates a minimal DecompilationContext internally.
  ///
  /// @param ast_unit The AST unit to transform
  /// @return Pipeline instance or error message
  static Result<std::unique_ptr<ASTTransformPipeline>, std::string> Create(
      clang::ASTUnit &ast_unit);

  /// Run all transformation passes in the standard sequence.
  /// Applies dead code elimination, control flow refinement,
  /// loop optimization, and expression simplification.
  /// Exceptions may be thrown if passes fail.
  void RunAllPasses();

  /// Stage 1: Remove unreachable code and provably dead branches.
  /// Uses Z3 to prove branch reachability.
  /// Exceptions may be thrown if pass fails.
  void EliminateDeadCode();

  /// Stage 2: Refine control flow structure (if/else chains, conditions).
  /// Runs multiple passes to fixpoint for iterative improvement.
  /// Exceptions may be thrown if passes fail.
  void RefineControlFlow();

  /// Stage 3: Optimize loop structures.
  /// Converts while(1) { if(cond) break; } to while(!cond).
  /// Runs to fixpoint.
  /// Exceptions may be thrown if passes fail.
  void OptimizeLoops();

  /// Stage 4: Simplify expressions and materialize conditions.
  /// Applies expression combining (*&x → x) and materializes Z3 conditions.
  /// Exceptions may be thrown if passes fail.
  void SimplifyExpressions();

  /// Get the AST context (provides access to the transformed AST).
  clang::ASTContext &GetAST();

  ~ASTTransformPipeline();

 private:
  ASTTransformPipeline(clang::ASTUnit &ast_unit,
                       std::unique_ptr<DecompilationContext> ctx);

  clang::ASTUnit &ast_unit;
  std::unique_ptr<DecompilationContext> dec_ctx;
};

}  // namespace rellic
