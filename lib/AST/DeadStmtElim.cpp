/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */
#include "rellic/AST/DeadStmtElim.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

namespace rellic {

DeadStmtElim::DeadStmtElim(Provenance &provenance, clang::ASTUnit &unit)
    : TransformVisitor<DeadStmtElim>(provenance, unit) {}

bool DeadStmtElim::VisitIfStmt(clang::IfStmt *ifstmt) {
  // DLOG(INFO) << "VisitIfStmt";
  bool expr_bool_value = false;
  auto if_const_expr = ifstmt->getCond()->getIntegerConstantExpr(ast_ctx);

  bool is_const = if_const_expr.hasValue();
  if (is_const) {
    expr_bool_value = if_const_expr->getBoolValue();
  }

  auto compound = clang::dyn_cast<clang::CompoundStmt>(ifstmt->getThen());
  bool is_empty = compound ? compound->body_empty() : false;
  if ((is_const && !expr_bool_value) || is_empty) {
    substitutions[ifstmt] = nullptr;
  }
  return true;
}

bool DeadStmtElim::VisitCompoundStmt(clang::CompoundStmt *compound) {
  // DLOG(INFO) << "VisitCompoundStmt";
  std::vector<clang::Stmt *> new_body;
  for (auto stmt : compound->body()) {
    // Filter out nullptr statements
    if (!stmt) {
      continue;
    }
    // Add only necessary statements
    if (auto expr = clang::dyn_cast<clang::Expr>(stmt)) {
      if (expr->HasSideEffects(ast_ctx)) {
        new_body.push_back(stmt);
      }
    } else if (!clang::dyn_cast<clang::NullStmt>(stmt)) {
      new_body.push_back(stmt);
    }
  }
  // Create the a new compound
  if (changed || new_body.size() < compound->size()) {
    substitutions[compound] = ast.CreateCompoundStmt(new_body);
  }
  return !Stopped();
}

void DeadStmtElim::RunImpl() {
  LOG(INFO) << "Eliminating dead statements";
  TransformVisitor<DeadStmtElim>::RunImpl();
  TraverseDecl(ast_ctx.getTranslationUnitDecl());
}

}  // namespace rellic
