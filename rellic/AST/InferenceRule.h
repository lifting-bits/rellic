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

#pragma once

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>

namespace rellic {

class InferenceRule : public clang::ast_matchers::MatchFinder::MatchCallback {
 protected:
  clang::ast_matchers::StatementMatcher cond;
  const clang::Stmt *match;
  clang::Stmt *substitution;

 public:
  InferenceRule(clang::ast_matchers::StatementMatcher matcher)
      : cond(matcher), match(nullptr), substitution(nullptr) {}

  operator bool() { return match; }

  const clang::ast_matchers::StatementMatcher &GetCondition() const {
    return cond;
  }

  virtual clang::Stmt *GetOrCreateSubstitution(clang::ASTContext &ctx,
                                               clang::Stmt *stmt) = 0;
};

clang::Stmt *ApplyFirstMatchingRule(clang::ASTContext &ctx, clang::Stmt *stmt,
                                    std::vector<InferenceRule *> &rules);

}  // namespace rellic