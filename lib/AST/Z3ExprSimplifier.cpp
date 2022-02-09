/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#define GOOGLE_STRIP_LOG 1

#include "rellic/AST/Z3ExprSimplifier.h"

#include <clang/Frontend/ASTUnit.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

namespace rellic {
Z3ExprSimplifier::Z3ExprSimplifier(clang::ASTUnit& unit)
    : ast_ctx(&unit.getASTContext()),
      ast(unit),
      z_ctx(new z3::context()),
      z_gen(new Z3ConvVisitor(unit, z_ctx.get())),
      tactic(*z_ctx, "sat") {}

bool Z3ExprSimplifier::Prove(z3::expr e) {
  z3::goal goal(*z_ctx);
  goal.add((!e).simplify());
  auto app{tactic.apply(goal)};
  CHECK(app.size() == 1) << "Unexpected multiple goals in application!";
  return app[0].is_decided_unsat();
}

z3::expr Z3ExprSimplifier::ToZ3(clang::Expr* e) {
  return z_gen->Z3BoolCast(z_gen->GetOrCreateZ3Expr(e));
}

clang::Expr* Z3ExprSimplifier::Simplify(clang::Expr* c_expr) {
  if (auto binop = clang::dyn_cast<clang::BinaryOperator>(c_expr)) {
    auto lhs{Simplify(binop->getLHS())};
    auto rhs{Simplify(binop->getRHS())};

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
    auto sub{Simplify(unop->getSubExpr())};
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
    auto sub{Simplify(paren->getSubExpr())};
    paren->setSubExpr(sub);
  }

  return c_expr;
}
}  // namespace rellic