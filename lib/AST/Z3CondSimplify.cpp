/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/Z3CondSimplify.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Expr.h>
#include <clang/AST/OperationKinds.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

enum SatValue { Tautology, Contradiction, Unknown };

namespace rellic {

char Z3CondSimplify::ID = 0;

Z3CondSimplify::Z3CondSimplify(clang::ASTUnit &unit)
    : ModulePass(Z3CondSimplify::ID),
      ast_ctx(&unit.getASTContext()),
      ast(unit),
      z_ctx(new z3::context()),
      z_gen(new rellic::Z3ConvVisitor(unit, z_ctx.get())),
      simplifier(*z_ctx, "simplify") {}

clang::Expr *Z3CondSimplify::SimplifyCExpr(clang::Expr *c_expr) {
  auto Evaluate = [this](clang::Expr *e) {
    auto z_expr{z_gen->Z3BoolCast(z_gen->GetOrCreateZ3Expr(e))};
    z3::goal goal(*z_ctx);
    goal.add(z_expr.simplify());
    auto app = simplifier(goal);
    CHECK(app.size() == 1) << "Unexpected multiple goals in application!";
    if (app[0].is_decided_unsat()) {
      return Contradiction;
    }
    goal.reset();
    goal.add(!(z_expr.simplify()));
    app = simplifier(goal);
    CHECK(app.size() == 1) << "Unexpected multiple goals in application!";

    if (app[0].is_decided_unsat()) {
      return Tautology;
    } else {
      return Unknown;
    }
  };

  if (auto binop = clang::dyn_cast<clang::BinaryOperator>(c_expr)) {
    auto lhs{SimplifyCExpr(binop->getLHS())};
    auto rhs{SimplifyCExpr(binop->getRHS())};

    auto lhs_eval{Evaluate(lhs)};
    auto rhs_eval{Evaluate(rhs)};

    switch (binop->getOpcode()) {
      case clang::BO_LAnd:
        if (lhs_eval == Tautology && rhs_eval == Tautology) {
          return ast.CreateTrue();
        } else if (lhs_eval == Contradiction || rhs_eval == Contradiction) {
          return ast.CreateFalse();
        } else if (lhs_eval == Tautology) {
          return rhs;
        } else if (rhs_eval == Tautology) {
          return lhs;
        }
      case clang::BO_LOr:
        if (lhs_eval == Contradiction && rhs_eval == Contradiction) {
          return ast.CreateFalse();
        } else if (lhs_eval == Tautology || rhs_eval == Tautology) {
          return ast.CreateTrue();
        } else if (lhs_eval == Contradiction) {
          return rhs;
        } else if (rhs_eval == Contradiction) {
          return lhs;
        }
      default:
        binop->setLHS(lhs);
        binop->setRHS(rhs);
        auto eval{Evaluate(binop)};
        if (eval == Tautology) {
          return ast.CreateTrue();
        } else if (eval == Contradiction) {
          return ast.CreateFalse();
        }
        break;
    }
  } else if (auto unop = clang::dyn_cast<clang::UnaryOperator>(c_expr)) {
    auto sub{SimplifyCExpr(unop->getSubExpr())};
    auto eval{Evaluate(sub)};
    switch (unop->getOpcode()) {
      case clang::UO_LNot:
        if (eval == Tautology) {
          return ast.CreateFalse();
        } else if (eval == Contradiction) {
          return ast.CreateTrue();
        }
      default:
        unop->setSubExpr(sub);
        break;
    }
  } else if (auto paren = clang::dyn_cast<clang::ParenExpr>(c_expr)) {
    auto sub{SimplifyCExpr(paren->getSubExpr())};
    paren->setSubExpr(sub);
  }

  return c_expr;
}

bool Z3CondSimplify::VisitIfStmt(clang::IfStmt *stmt) {
  stmt->setCond(SimplifyCExpr(stmt->getCond()));
  return true;
}

bool Z3CondSimplify::VisitWhileStmt(clang::WhileStmt *loop) {
  loop->setCond(SimplifyCExpr(loop->getCond()));
  return true;
}

bool Z3CondSimplify::VisitDoStmt(clang::DoStmt *loop) {
  loop->setCond(SimplifyCExpr(loop->getCond()));
  return true;
}

bool Z3CondSimplify::runOnModule(llvm::Module &module) {
  LOG(INFO) << "Simplifying conditions using Z3";
  Initialize();
  TraverseDecl(ast_ctx->getTranslationUnitDecl());
  return changed;
}

}  // namespace rellic
