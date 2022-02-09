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

namespace rellic {

char Z3CondSimplify::ID = 0;

Z3CondSimplify::Z3CondSimplify(clang::ASTUnit &unit)
    : ModulePass(Z3CondSimplify::ID),
      ast_ctx(&unit.getASTContext()),
      ast(unit),
      z_ctx(new z3::context()),
      z_gen(new rellic::Z3ConvVisitor(unit, z_ctx.get())),
      simplifier(*z_ctx, "sat") {}

clang::Expr *Z3CondSimplify::SimplifyCExpr(clang::Expr *c_expr) {
  auto Prove = [this](z3::expr e) {
    z3::goal goal(*z_ctx);
    goal.add((!e).simplify());
    auto app{simplifier.apply(goal)};
    CHECK(app.size() == 1) << "Unexpected multiple goals in application!";
    return app[0].is_decided_unsat();
  };

  auto ToZ3 = [this](clang::Expr *e) {
    return z_gen->Z3BoolCast(z_gen->GetOrCreateZ3Expr(e));
  };

  if (auto binop = clang::dyn_cast<clang::BinaryOperator>(c_expr)) {
    auto lhs{SimplifyCExpr(binop->getLHS())};
    auto rhs{SimplifyCExpr(binop->getRHS())};

    auto z_lhs{ToZ3(lhs)};
    auto z_rhs{ToZ3(rhs)};

    auto lhs_proven{Prove(z_lhs)};
    auto rhs_proven{Prove(z_rhs)};
    auto not_lhs_proven{Prove(!z_lhs)};
    auto not_rhs_proven{Prove(!z_rhs)};

    switch (binop->getOpcode()) {
      case clang::BO_LAnd:
        if (lhs_proven && rhs_proven) {
          return ast.CreateTrue();
        } else if (not_lhs_proven || not_rhs_proven) {
          return ast.CreateFalse();
        } else if (lhs_proven) {
          return rhs;
        } else if (rhs_proven) {
          return lhs;
        }
      case clang::BO_LOr:
        if (not_lhs_proven && not_rhs_proven) {
          return ast.CreateFalse();
        } else if (lhs_proven || rhs_proven) {
          return ast.CreateTrue();
        } else if (not_lhs_proven) {
          return rhs;
        } else if (not_rhs_proven) {
          return lhs;
        }
      default:
        binop->setLHS(lhs);
        binop->setRHS(rhs);
        auto z_binop{ToZ3(binop)};
        if (Prove(z_binop)) {
          return ast.CreateTrue();
        } else if (Prove(!z_binop)) {
          return ast.CreateFalse();
        }
        break;
    }
  } else if (auto unop = clang::dyn_cast<clang::UnaryOperator>(c_expr)) {
    auto sub{SimplifyCExpr(unop->getSubExpr())};
    auto z_sub{ToZ3(sub)};
    switch (unop->getOpcode()) {
      case clang::UO_LNot:
        if (Prove(z_sub)) {
          return ast.CreateFalse();
        } else if (Prove(!z_sub)) {
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
