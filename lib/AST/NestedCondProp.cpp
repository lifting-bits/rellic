/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/NestedCondProp.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/Util.h"

namespace rellic {

namespace {

static std::vector<clang::IfStmt *> GetIfStmts(clang::CompoundStmt *compound) {
  std::vector<clang::IfStmt *> result;
  for (auto stmt : compound->body()) {
    if (auto ifstmt = clang::dyn_cast<clang::IfStmt>(stmt)) {
      result.push_back(ifstmt);
    }
  }
  return result;
}

}  // namespace

NestedCondProp::NestedCondProp(StmtToIRMap &provenance, clang::ASTUnit &unit,
                               Substitutions &substitutions)
    : ASTPass(provenance, unit, substitutions),
      z3_ctx(new z3::context()),
      z3_gen(new rellic::Z3ConvVisitor(unit, z3_ctx.get())) {}

static clang::Expr *Negate(rellic::ASTBuilder &ast, clang::Expr *expr) {
  if (auto binop = clang::dyn_cast<clang::BinaryOperator>(expr)) {
    if (binop->isComparisonOp()) {
      auto opc = clang::BinaryOperator::negateComparisonOp(binop->getOpcode());
      return ast.CreateBinaryOp(opc, binop->getLHS(), binop->getRHS());
    }
  } else if (auto unop = clang::dyn_cast<clang::UnaryOperator>(expr)) {
    if (unop->getOpcode() == clang::UO_LNot) {
      return unop->getSubExpr();
    }
  }
  return ast.CreateLNot(expr);
}

void NestedCondProp::VisitIfStmt(clang::IfStmt *ifstmt) {
  // DLOG(INFO) << "VisitIfStmt";
  // Determine whether `cond` is a constant expression
  auto cond = ifstmt->getCond();
  if (!cond->isIntegerConstantExpr(ast_ctx)) {
    auto stmt_then = ifstmt->getThen();
    auto stmt_else = ifstmt->getElse();
    // `cond` is not a constant expression and we propagate it
    // to `clang::IfStmt` nodes in it's `then` branch.
    if (auto comp = clang::dyn_cast<clang::CompoundStmt>(stmt_then)) {
      for (auto child : GetIfStmts(comp)) {
        parent_conds[child] = cond;
      }
    } else {
      LOG(FATAL) << "Then branch must be a clang::CompoundStmt!";
    }
    if (stmt_else) {
      if (auto comp = clang::dyn_cast<clang::CompoundStmt>(stmt_else)) {
        for (auto child : GetIfStmts(comp)) {
          parent_conds[child] = Negate(ast, cond);
        }
      } else if (auto elif = clang::dyn_cast<clang::IfStmt>(stmt_else)) {
        parent_conds[elif] = Negate(ast, cond);
      } else {
        LOG(FATAL)
            << "Else branch must be a clang::CompoundStmt or clang::IfStmt!";
      }
    }
  }
  // Retrieve a parent `clang::Stmt` condition
  // and remove it from `cond` if it's present.
  auto iter = parent_conds.find(ifstmt);
  if (iter != parent_conds.end()) {
    auto child_expr{ifstmt->getCond()};
    auto parent_expr{iter->second};
    Replace(/*from=*/parent_expr,
            /*to=*/ast.CreateTrue(), /*in=*/child_expr, substitutions);
  }
}

void NestedCondProp::VisitWhileStmt(clang::WhileStmt *stmt) {
  auto cond = stmt->getCond();
  if (!cond->isIntegerConstantExpr(ast_ctx)) {
    auto body = stmt->getBody();
    // `cond` is not a constant expression and we propagate it
    // to `clang::IfStmt` nodes in its body.
    if (auto comp = clang::dyn_cast<clang::CompoundStmt>(body)) {
      for (auto child : GetIfStmts(comp)) {
        parent_conds[child] = cond;
      }
    } else {
      LOG(FATAL) << "While body must be a clang::CompoundStmt!";
    }
  }
  // Retrieve a parent `clang::Stmt` condition
  // and remove it from `cond` if it's present.
  auto iter = parent_conds.find(stmt);
  if (iter != parent_conds.end()) {
    auto child_expr{stmt->getCond()};
    auto parent_expr{iter->second};
    Replace(/*from=*/parent_expr,
            /*to=*/ast.CreateTrue(), /*in=*/child_expr, substitutions);
  }
}

void NestedCondProp::RunImpl(clang::Stmt *stmt) {
  LOG(INFO) << "Propagating nested conditions";
  Visit(stmt);
}

}  // namespace rellic