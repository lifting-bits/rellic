/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/NestedScopeCombine.h"

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/Support/raw_ostream.h>

#include "rellic/AST/Compat/Stmt.h"

namespace rellic {

NestedScopeCombine::NestedScopeCombine(StmtToIRMap &provenance,
                                       clang::ASTUnit &unit,
                                       Substitutions &substitutions)
    : ASTPass(provenance, unit, substitutions) {}

void NestedScopeCombine::VisitIfStmt(clang::IfStmt *ifstmt) {
  // DLOG(INFO) << "VisitIfStmt";
  // Determine whether `cond` is a constant expression that is always true and
  // `ifstmt` should be replaced by `then` in it's parent nodes.
  auto if_const_expr = rellic::GetIntegerConstantExprFromIf(ifstmt, ast_ctx);
  bool is_const = if_const_expr.hasValue();
  if (is_const && if_const_expr->getBoolValue()) {
    substitutions.push_back({ifstmt, ifstmt->getThen(), "NestedScopeCombine"});
  } else if (is_const && !if_const_expr->getBoolValue()) {
    if (auto else_stmt = ifstmt->getElse()) {
      substitutions.push_back({ifstmt, else_stmt, "NestedScopeCombine"});
    } else {
      substitutions.push_back(
          {ifstmt, ast.CreateNullStmt(), "NestedScopeCombine"});
    }
  }
}

void NestedScopeCombine::VisitCompoundStmt(clang::CompoundStmt *compound) {
  // DLOG(INFO) << "VisitCompoundStmt";
  bool has_compound = false;
  std::vector<clang::Stmt *> new_body;
  for (auto stmt : compound->body()) {
    if (auto child = clang::dyn_cast<clang::CompoundStmt>(stmt)) {
      new_body.insert(new_body.end(), child->body_begin(), child->body_end());
      has_compound = true;
    } else {
      new_body.push_back(stmt);
    }
  }

  if (has_compound) {
    substitutions.push_back(
        {compound, ast.CreateCompoundStmt(new_body), "NestedScopeCombine"});
  }
}

void NestedScopeCombine::RunImpl(clang::Stmt *stmt) {
  LOG(INFO) << "Combining nested scopes";
  Visit(stmt);
}

}  // namespace rellic
