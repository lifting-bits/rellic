/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/LoopCondProp.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/Stmt.h>
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

  bool VisitCompoundStmt(clang::CompoundStmt* compound, ExprVec& trueExprs) {
    bool changed{false};
    for (auto stmt : compound->body()) {
      changed |= Visit(stmt, trueExprs);
    }

    return changed;
  }

  bool VisitWhileStmt(clang::WhileStmt* whileStmt, ExprVec& trueExprs) {
    bool changed{false};
    auto cond{whileStmt->getCond()};
    for (auto trueExpr : trueExprs) {
      changed |= Replace(trueExpr, ast.CreateTrue(), &cond);
    }
    whileStmt->setCond(cond);

    ExprVec inner{trueExprs};
    if (!isConstant(ctx, whileStmt->getCond())) {
      inner.push_back(whileStmt->getCond());
    }
    changed |= Visit(whileStmt->getBody(), inner);

    trueExprs.push_back(Negate(ast, whileStmt->getCond()));
    return changed;
  }

  bool VisitDoStmt(clang::DoStmt* doStmt, ExprVec& trueExprs) {
    bool changed{false};
    auto cond{doStmt->getCond()};
    for (auto trueExpr : trueExprs) {
      changed |= Replace(trueExpr, ast.CreateTrue(), &cond);
    }
    doStmt->setCond(cond);

    ExprVec inner{trueExprs};
    Visit(doStmt->getBody(), inner);

    trueExprs.push_back(Negate(ast, doStmt->getCond()));
    return changed;
  }

  bool VisitIfStmt(clang::IfStmt* ifStmt, ExprVec& trueExprs) {
    bool changed{false};
    auto cond{ifStmt->getCond()};
    for (auto trueExpr : trueExprs) {
      changed |= Replace(trueExpr, ast.CreateTrue(), &cond);
    }
    ifStmt->setCond(cond);

    ExprVec innerThen{trueExprs};
    if (!isConstant(ctx, ifStmt->getCond())) {
      innerThen.push_back(ifStmt->getCond());
    }
    Visit(ifStmt->getThen(), innerThen);

    if (ifStmt->getElse()) {
      ExprVec innerElse{trueExprs};
      innerElse.push_back(Negate(ast, ifStmt->getCond()));
      Visit(ifStmt->getElse(), innerThen);
    }
    return changed;
  }
};

LoopCondProp::LoopCondProp(Provenance& provenance, clang::ASTUnit& unit)
    : ASTPass(provenance, unit) {}

void LoopCondProp::RunImpl() {
  changed = false;
  ASTBuilder ast{ast_unit};
  CompoundVisitor visitor{ast, ast_ctx};

  for (auto decl : ast_ctx.getTranslationUnitDecl()->decls()) {
    if (auto fdecl = clang::dyn_cast<clang::FunctionDecl>(decl)) {
      if (fdecl->hasBody()) {
        ExprVec trueExprs;
        changed |= visitor.Visit(fdecl->getBody(), trueExprs);
      }
    }
  }
}
}  // namespace rellic