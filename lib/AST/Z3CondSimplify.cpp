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

Z3CondSimplify::Z3CondSimplify(StmtToIRMap &provenance, clang::ASTUnit &unit,
                               Substitutions &substitutions)
    : ASTPass(provenance, unit, substitutions),
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

void Z3CondSimplify::VisitBinaryOperator(clang::BinaryOperator *binop) {
  auto lhs{binop->getLHS()};
  auto rhs{binop->getRHS()};
  auto opcode{binop->getOpcode()};
  if (opcode == clang::BO_LAnd || opcode == clang::BO_LOr) {
    auto lhs_proven{IsProvenTrue(lhs)};
    auto rhs_proven{IsProvenTrue(rhs)};
    auto not_lhs_proven{IsProvenFalse(lhs)};
    auto not_rhs_proven{IsProvenFalse(rhs)};
    if (opcode == clang::BO_LAnd) {
      if (lhs_proven && rhs_proven) {
        substitutions.push_back({binop, ast.CreateTrue(), "Z3CondSimplify"});
      } else if (not_lhs_proven || not_rhs_proven) {
        substitutions.push_back({binop, ast.CreateFalse(), "Z3CondSimplify"});
      } else if (lhs_proven) {
        substitutions.push_back({binop, rhs, "Z3CondSimplify"});
      } else if (rhs_proven) {
        substitutions.push_back({binop, lhs, "Z3CondSimplify"});
      }
    } else {
      if (not_lhs_proven && not_rhs_proven) {
        substitutions.push_back({binop, ast.CreateFalse(), "Z3CondSimplify"});
      } else if (lhs_proven || rhs_proven) {
        substitutions.push_back({binop, ast.CreateTrue(), "Z3CondSimplify"});
      } else if (not_lhs_proven) {
        substitutions.push_back({binop, rhs, "Z3CondSimplify"});
      } else if (not_rhs_proven) {
        substitutions.push_back({binop, lhs, "Z3CondSimplify"});
      }
    }
  }
}

void Z3CondSimplify::VisitUnaryOperator(clang::UnaryOperator *unop) {
  if (unop->getOpcode() == clang::UO_LNot) {
    auto sub{unop->getSubExpr()};
    if (IsProvenTrue(sub)) {
      substitutions.push_back({unop, ast.CreateFalse(), "Z3CondSimplify"});
    } else if (IsProvenFalse(sub)) {
      substitutions.push_back({unop, ast.CreateTrue(), "Z3CondSimplify"});
    }
  }
}

void Z3CondSimplify::RunImpl(clang::Stmt *stmt) {
  LOG(INFO) << "Simplifying conditions using Z3";
  Visit(stmt);
}

}  // namespace rellic
