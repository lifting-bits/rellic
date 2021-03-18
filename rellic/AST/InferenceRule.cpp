/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/InferenceRule.h"

namespace rellic {

clang::Stmt *ApplyFirstMatchingRule(clang::ASTContext &ctx, clang::Stmt *stmt,
                                    std::vector<InferenceRule *> &rules) {
  clang::ast_matchers::MatchFinder::MatchFinderOptions opts;
  clang::ast_matchers::MatchFinder finder(opts);

  for (auto rule : rules) {
    finder.addMatcher(rule->GetCondition(), rule);
  }

  finder.match(*stmt, ctx);

  for (auto rule : rules) {
    if (*rule) {
      return rule->GetOrCreateSubstitution(ctx, stmt);
    }
  }

  return stmt;
}

}  // namespace rellic