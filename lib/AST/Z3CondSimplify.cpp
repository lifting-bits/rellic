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

Z3CondSimplify::Z3CondSimplify(Provenance &provenance, clang::ASTUnit &unit)
    : TransformVisitor<Z3CondSimplify>(provenance, unit),
      z_ctx(new z3::context()),
      z_gen(new Z3ConvVisitor(unit, z_ctx.get())),
      tactic(z3::tactic(*z_ctx, "sat")),
      hash_adaptor{ast_ctx, hashes},
      ke_adaptor{ast_ctx},
      proven_true(10, hash_adaptor, ke_adaptor),
      proven_false(10, hash_adaptor, ke_adaptor) {}

bool Z3CondSimplify::IsProvenTrue(clang::Expr *e) {
  auto it{proven_true.find(e)};
  if (it == proven_true.end()) {
    proven_true[e] = Prove(ToZ3(e));
  }
  return proven_true[e];
}

bool Z3CondSimplify::IsProvenFalse(clang::Expr *e) {
  auto it{proven_false.find(e)};
  if (it == proven_false.end()) {
    proven_false[e] = Prove(!ToZ3(e));
  }
  return proven_false[e];
}

bool Z3CondSimplify::Prove(z3::expr e) {
  z3::goal goal(*z_ctx);
  goal.add((!e).simplify());
  auto app{tactic.apply(goal)};
  CHECK(app.size() == 1) << "Unexpected multiple goals in application!";
  return app[0].is_decided_unsat();
}

z3::expr Z3CondSimplify::ToZ3(clang::Expr *e) {
  return z_gen->Z3BoolCast(z_gen->GetOrCreateZ3Expr(e));
}

clang::Expr *Z3CondSimplify::Simplify(clang::Expr *c_expr) {
  if (auto binop = clang::dyn_cast<clang::BinaryOperator>(c_expr)) {
    auto lhs{Simplify(binop->getLHS())};
    auto rhs{Simplify(binop->getRHS())};

    auto opcode{binop->getOpcode()};
    if (opcode == clang::BO_LAnd || opcode == clang::BO_LOr) {
      auto lhs_proven{IsProvenTrue(lhs)};
      auto rhs_proven{IsProvenTrue(rhs)};
      auto not_lhs_proven{IsProvenFalse(lhs)};
      auto not_rhs_proven{IsProvenFalse(rhs)};
      if (opcode == clang::BO_LAnd) {
        if (lhs_proven && rhs_proven) {
          changed = true;
          return ast.CreateTrue();
        } else if (not_lhs_proven || not_rhs_proven) {
          changed = true;
          return ast.CreateFalse();
        } else if (lhs_proven) {
          changed = true;
          return rhs;
        } else if (rhs_proven) {
          changed = true;
          return lhs;
        }
      } else {
        if (not_lhs_proven && not_rhs_proven) {
          changed = true;
          return ast.CreateFalse();
        } else if (lhs_proven || rhs_proven) {
          changed = true;
          return ast.CreateTrue();
        } else if (not_lhs_proven) {
          changed = true;
          return rhs;
        } else if (not_rhs_proven) {
          changed = true;
          return lhs;
        }
      }

      binop->setLHS(lhs);
      binop->setRHS(rhs);
      hashes[binop] = 0;
      if (IsProvenTrue(binop)) {
        changed = true;
        return ast.CreateTrue();
      } else if (IsProvenFalse(binop)) {
        changed = true;
        return ast.CreateFalse();
      }
    }
  } else if (auto unop = clang::dyn_cast<clang::UnaryOperator>(c_expr)) {
    if (unop->getOpcode() == clang::UO_LNot) {
      auto sub{Simplify(unop->getSubExpr())};
      if (IsProvenTrue(sub)) {
        changed = true;
        return ast.CreateFalse();
      } else if (IsProvenFalse(sub)) {
        changed = true;
        return ast.CreateTrue();
      }
      unop->setSubExpr(sub);
      hashes[unop] = 0;
    }
  } else if (auto paren = clang::dyn_cast<clang::ParenExpr>(c_expr)) {
    auto sub{Simplify(paren->getSubExpr())};
    paren->setSubExpr(sub);
    hashes[paren] = 0;
  }

  return c_expr;
}

bool Z3CondSimplify::VisitIfStmt(clang::IfStmt *stmt) {
  stmt->setCond(Simplify(stmt->getCond()));
  return true;
}

bool Z3CondSimplify::VisitWhileStmt(clang::WhileStmt *loop) {
  loop->setCond(Simplify(loop->getCond()));
  return true;
}

bool Z3CondSimplify::VisitDoStmt(clang::DoStmt *loop) {
  loop->setCond(Simplify(loop->getCond()));
  return true;
}

void Z3CondSimplify::RunImpl() {
  LOG(INFO) << "Simplifying conditions using Z3";
  TransformVisitor<Z3CondSimplify>::RunImpl();
  TraverseDecl(ast_ctx.getTranslationUnitDecl());
}

}  // namespace rellic
