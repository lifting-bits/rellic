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

#include <iterator>

namespace rellic {

CondBasedRefine::CondBasedRefine(DecompilationContext &dec_ctx)
    : TransformVisitor<CondBasedRefine>(dec_ctx) {}

bool CondBasedRefine::VisitCompoundStmt(clang::CompoundStmt *compound) {
  std::vector<clang::Stmt *> body{compound->body_begin(), compound->body_end()};
  bool did_something{false};

  for (size_t i{0}; i + 1 < body.size() && !did_something; ++i) {
    auto if_a{clang::dyn_cast<clang::IfStmt>(body[i])};
    auto if_b{clang::dyn_cast<clang::IfStmt>(body[i + 1])};

    // We need two `if` statements to combine
    if (!if_a || !if_b) {
      continue;
    }

    auto cond_a{dec_ctx.z3_exprs[dec_ctx.conds[if_a]]};
    auto cond_b{dec_ctx.z3_exprs[dec_ctx.conds[if_b]]};

    auto then_a{if_a->getThen()};
    auto then_b{if_b->getThen()};

    auto else_a{if_a->getElse()};
    auto else_b{if_b->getElse()};

    std::vector<clang::Stmt *> new_then_body{then_a};
    clang::IfStmt *new_if{nullptr};
    if (Prove(cond_a == cond_b)) {
      // We found two consecutive `if` statements with identical conditions, so
      // we can merge their `then` and `else` branches
      //
      // if(a) { X1; } else { Y1; }
      // if(a) { X2; } else { Y2; }
      // becomes
      // if(a) { X1; X2; } else { Y1; Y2; }
      new_then_body.push_back(then_b);
      auto new_then{dec_ctx.ast.CreateCompoundStmt(new_then_body)};

      new_if = dec_ctx.ast.CreateIf(dec_ctx.marker_expr, new_then);

      if (else_a || else_b) {
        // At least one of the two `if` statements has an `else` branch
        std::vector<clang::Stmt *> new_else_body{};

        if (else_a) {
          new_else_body.push_back(else_a);
        }

        if (else_b) {
          new_else_body.push_back(else_b);
        }

        auto new_else{dec_ctx.ast.CreateCompoundStmt(new_else_body)};
        new_if->setElse(new_else);
      }

      did_something = true;
    } else if (Prove(cond_a == !cond_b)) {
      // We found two consecutive `if` statements with opposite conditions, so
      // we can append the else branch of the second to the then branch of the
      // first, and viceversa
      //
      // if(a) { X1; } else { Y1; }
      // if(!a) { X2; } else { Y2; }
      // becomes
      // if(a) { X1; Y2; } else { Y1; X2; }
      if (else_b) {
        new_then_body.push_back(else_b);
      }

      auto new_then{dec_ctx.ast.CreateCompoundStmt(new_then_body)};

      std::vector<clang::Stmt *> new_else_body{};
      if (else_a) {
        new_else_body.push_back(else_a);
      }
      new_else_body.push_back(then_b);

      new_if = dec_ctx.ast.CreateIf(dec_ctx.marker_expr, new_then);

      auto new_else{dec_ctx.ast.CreateCompoundStmt(new_else_body)};
      new_if->setElse(new_else);

      did_something = true;
    }

    if (did_something) {
      dec_ctx.conds[new_if] = dec_ctx.conds[if_a];
      body[i] = new_if;
      body.erase(std::next(body.begin(), i + 1));
    }
  }
  if (did_something) {
    substitutions[compound] = dec_ctx.ast.CreateCompoundStmt(body);
  }
  return !Stopped();
}

void CondBasedRefine::RunImpl() {
  LOG(INFO) << "Condition-based refinement";
  TransformVisitor<CondBasedRefine>::RunImpl();
  TraverseDecl(dec_ctx.ast_ctx.getTranslationUnitDecl());
}

}  // namespace rellic