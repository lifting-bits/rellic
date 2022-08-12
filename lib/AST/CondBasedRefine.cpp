/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/CondBasedRefine.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <cstddef>

#include "rellic/AST/Util.h"

namespace rellic {

CondBasedRefine::CondBasedRefine(Provenance &provenance, clang::ASTUnit &unit)
    : TransformVisitor<CondBasedRefine>(provenance, unit),
      z3_ctx(new z3::context()),
      z3_gen(new rellic::Z3ConvVisitor(unit, z3_ctx.get())),
      z3_solver(*z3_ctx, "sat") {}

z3::expr CondBasedRefine::GetZ3Cond(clang::IfStmt *ifstmt) {
  auto cond = ifstmt->getCond();
  auto expr = z3_gen->Z3BoolCast(z3_gen->GetOrCreateZ3Expr(cond));
  return expr.simplify();
}

bool CondBasedRefine::VisitCompoundStmt(clang::CompoundStmt *compound) {
  std::vector<clang::Stmt *> body{compound->body_begin(), compound->body_end()};
  bool did_something{false};
  for (size_t i{0}; i + 1 < body.size() && !Stopped(); ++i) {
    auto if_a{clang::dyn_cast<clang::IfStmt>(body[i])};
    auto if_b{clang::dyn_cast<clang::IfStmt>(body[i + 1])};

    if (!if_a || !if_b) {
      continue;
    }

    auto then_a{if_a->getThen()};
    auto then_b{if_b->getThen()};

    auto else_a{if_a->getElse()};
    auto else_b{if_b->getElse()};

    auto cond_a{GetZ3Cond(if_a)};
    auto cond_b{GetZ3Cond(if_b)};

    clang::IfStmt *new_if{};
    if (Prove(*z3_ctx, cond_a == cond_b)) {
      std::vector<clang::Stmt *> new_then_body{then_a, then_b};
      auto new_then{ast.CreateCompoundStmt(new_then_body)};
      new_if = ast.CreateIf(if_a->getCond(), new_then);

      if (else_a || else_b) {
        std::vector<clang::Stmt *> new_else_body;
        if (else_a) {
          new_else_body.push_back(else_a);
        }
        if (else_b) {
          new_else_body.push_back(else_b);
        }
        new_if->setElse(ast.CreateCompoundStmt(new_else_body));
      }
    } else if (Prove(*z3_ctx, cond_a == !cond_b)) {
      std::vector<clang::Stmt *> new_then_body{then_a};
      if (else_b) {
        new_then_body.push_back(else_b);
      }
      auto new_then{ast.CreateCompoundStmt(new_then_body)};
      new_if = ast.CreateIf(if_a->getCond(), new_then);

      std::vector<clang::Stmt *> new_else_body;
      if (else_a) {
        new_else_body.push_back(else_a);
      }
      new_else_body.push_back(then_b);
      new_if->setElse(ast.CreateCompoundStmt(new_else_body));
    }

    if (new_if) {
      body[i] = new_if;
      body.erase(std::next(body.begin(), i + 1));
      did_something = true;
    }
  }

  if (did_something) {
    substitutions[compound] = ast.CreateCompoundStmt(body);
  }
  return !Stopped();
}

void CondBasedRefine::RunImpl() {
  LOG(INFO) << "Condition-based refinement";
  TransformVisitor<CondBasedRefine>::RunImpl();
  TraverseDecl(ast_ctx.getTranslationUnitDecl());
}

}  // namespace rellic