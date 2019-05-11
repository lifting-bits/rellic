/*
 * Copyright (c) 2018 Trail of Bits, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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