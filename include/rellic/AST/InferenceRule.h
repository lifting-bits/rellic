/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>

#include "rellic/AST/IRToASTVisitor.h"

namespace clang {
class ASTUnit;
}

namespace rellic {

class InferenceRule : public clang::ast_matchers::MatchFinder::MatchCallback {
 protected:
  clang::ast_matchers::StatementMatcher cond;
  const clang::Stmt *match;

  static void CopyProvenance(clang::Stmt *from, clang::Stmt *to,
                             StmtToIRMap &provenance);

 public:
  InferenceRule(clang::ast_matchers::StatementMatcher matcher)
      : cond(matcher), match(nullptr) {}

  operator bool() { return match; }

  const clang::ast_matchers::StatementMatcher &GetCondition() const {
    return cond;
  }

  virtual clang::Stmt *GetOrCreateSubstitution(StmtToIRMap &provenance,
                                               clang::ASTUnit &unit,
                                               clang::Stmt *stmt) = 0;
};

clang::Stmt *ApplyFirstMatchingRule(
    StmtToIRMap &provenance, clang::ASTUnit &unit, clang::Stmt *stmt,
    std::vector<std::unique_ptr<InferenceRule>> &rules);

}  // namespace rellic