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

#include "rellic/AST/Compat/Stmt.h"

namespace rellic {

NestedScopeCombine::NestedScopeCombine(StmtToIRMap &provenance,
                                       clang::ASTUnit &unit)
    : TransformVisitor<NestedScopeCombine>(provenance, unit) {}

bool NestedScopeCombine::VisitIfStmt(clang::IfStmt *ifstmt) {
  // DLOG(INFO) << "VisitIfStmt";
  // Determine whether `cond` is a constant expression that is always true and
  // `ifstmt` should be replaced by `then` in it's parent nodes.
  auto if_const_expr = rellic::GetIntegerConstantExprFromIf(ifstmt, ast_ctx);
  bool is_const = if_const_expr.hasValue();
  if (is_const && if_const_expr->getBoolValue()) {
    substitutions[ifstmt] = ifstmt->getThen();
  } else if (is_const && !if_const_expr->getBoolValue()) {
    if (auto else_stmt = ifstmt->getElse()) {
      substitutions[ifstmt] = else_stmt;
    } else {
      std::vector<clang::Stmt *> body;
      substitutions[ifstmt] = ast.CreateCompoundStmt(body);
    }
  }
  return !Stopped();
}

bool NestedScopeCombine::VisitCompoundStmt(clang::CompoundStmt *compound) {
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
    substitutions[compound] = ast.CreateCompoundStmt(new_body);
  }

  return !Stopped();
}

void NestedScopeCombine::RunImpl() {
  LOG(INFO) << "Combining nested scopes";
  TransformVisitor<NestedScopeCombine>::RunImpl();
  TraverseDecl(ast_ctx.getTranslationUnitDecl());
}

}  // namespace rellic
