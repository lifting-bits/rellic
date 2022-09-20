/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/InferenceRule.h"

#include <clang/AST/Stmt.h>
#include <clang/Frontend/ASTUnit.h>

namespace rellic {

clang::Stmt *ApplyFirstMatchingRule(
    DecompilationContext &dec_ctx, clang::Stmt *stmt,
    std::vector<std::unique_ptr<InferenceRule>> &rules) {
  clang::ast_matchers::MatchFinder::MatchFinderOptions opts;
  clang::ast_matchers::MatchFinder finder(opts);

  for (auto &rule : rules) {
    finder.addMatcher(rule->GetCondition(), rule.get());
  }

  finder.match(*stmt, dec_ctx.ast_unit.getASTContext());

  for (auto &rule : rules) {
    if (*rule) {
      return rule->GetOrCreateSubstitution(dec_ctx, stmt);
    }
  }

  return stmt;
}

}  // namespace rellic