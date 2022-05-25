/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/NestedCondProp.h"

#include <clang/AST/StmtVisitor.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include <vector>

#include "rellic/AST/ASTBuilder.h"
#include "rellic/AST/Util.h"

namespace rellic {
using ExprVec = std::vector<clang::Expr*>;

static bool isConstant(const clang::ASTContext& ctx, clang::Expr* expr) {
  return expr->getIntegerConstantExpr(ctx).hasValue();
}

class CompoundVisitor
    : public clang::StmtVisitor<CompoundVisitor, bool, ExprVec&> {
 private:
  ASTBuilder& ast;
  clang::ASTContext& ctx;

 public:
  CompoundVisitor(ASTBuilder& ast, clang::ASTContext& ctx)
      : ast(ast), ctx(ctx) {}

  bool VisitCompoundStmt(clang::CompoundStmt* compound, ExprVec& true_exprs) {
    bool changed{false};
    for (auto stmt : compound->body()) {
      changed |= Visit(stmt, true_exprs);
    }

    return changed;
  }

  bool VisitWhileStmt(clang::WhileStmt* while_stmt, ExprVec& true_exprs) {
    bool changed{false};
    auto cond{while_stmt->getCond()};
    for (auto true_expr : true_exprs) {
      changed |= Replace(true_expr, ast.CreateTrue(), &cond);
    }
    while_stmt->setCond(cond);

    ExprVec inner{true_exprs};
    if (!isConstant(ctx, while_stmt->getCond())) {
      inner.push_back(while_stmt->getCond());
    }
    changed |= Visit(while_stmt->getBody(), inner);

    true_exprs.push_back(Negate(ast, while_stmt->getCond()));
    return changed;
  }

  bool VisitDoStmt(clang::DoStmt* do_stmt, ExprVec& true_exprs) {
    bool changed{false};
    auto cond{do_stmt->getCond()};
    for (auto true_expr : true_exprs) {
      changed |= Replace(true_expr, ast.CreateTrue(), &cond);
    }
    do_stmt->setCond(cond);

    ExprVec inner{true_exprs};
    Visit(do_stmt->getBody(), inner);

    true_exprs.push_back(Negate(ast, do_stmt->getCond()));
    return changed;
  }

  bool VisitIfStmt(clang::IfStmt* if_stmt, ExprVec& true_exprs) {
    bool changed{false};
    auto cond{if_stmt->getCond()};
    for (auto true_expr : true_exprs) {
      changed |= Replace(true_expr, ast.CreateTrue(), &cond);
    }
    if_stmt->setCond(cond);

    ExprVec inner_then{true_exprs};
    if (!isConstant(ctx, if_stmt->getCond())) {
      inner_then.push_back(if_stmt->getCond());
    }
    Visit(if_stmt->getThen(), inner_then);

    if (if_stmt->getElse()) {
      ExprVec inner_else{true_exprs};
      inner_else.push_back(Negate(ast, if_stmt->getCond()));
      Visit(if_stmt->getElse(), inner_else);
    }
    return changed;
  }
};

NestedCondProp::NestedCondProp(Provenance& provenance, clang::ASTUnit& unit)
    : ASTPass(provenance, unit) {}

void NestedCondProp::RunImpl() {
  changed = false;
  ASTBuilder ast{ast_unit};
  CompoundVisitor visitor{ast, ast_ctx};

  for (auto decl : ast_ctx.getTranslationUnitDecl()->decls()) {
    if (auto fdecl = clang::dyn_cast<clang::FunctionDecl>(decl)) {
      if (fdecl->hasBody()) {
        ExprVec true_exprs;
        changed |= visitor.Visit(fdecl->getBody(), true_exprs);
      }
    }
  }
}
}  // namespace rellic