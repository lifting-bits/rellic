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

CondBasedRefine::CondBasedRefine(Provenance &provenance, clang::ASTUnit &unit)
    : TransformVisitor<CondBasedRefine>(provenance, unit) {}

bool CondBasedRefine::VisitCompoundStmt(clang::CompoundStmt *compound) {
  std::vector<clang::Stmt *> body{compound->body_begin(), compound->body_end()};
  std::vector<clang::Stmt *> new_body{body};
  bool did_something{false};

  for (size_t i{0}; i + 1 < body.size(); ++i) {
    auto if_a{clang::dyn_cast<clang::IfStmt>(body[i])};
    auto if_b{clang::dyn_cast<clang::IfStmt>(body[i + 1])};

    if (!if_a || !if_b) {
      continue;
    }

    auto cond_a{provenance.z3_exprs[provenance.conds[if_a]]};
    auto cond_b{provenance.z3_exprs[provenance.conds[if_b]]};

    auto then_a{if_a->getThen()};
    auto then_b{if_b->getThen()};

    auto else_a{if_a->getElse()};
    auto else_b{if_b->getElse()};

    if (Prove(provenance.z3_ctx, cond_a == cond_b)) {
      std::vector<clang::Stmt *> new_then_body{then_a, then_b};
      auto new_then{ast.CreateCompoundStmt(new_then_body)};

      auto new_if{ast.CreateIf(provenance.marker_expr, new_then)};

      if (else_a || else_b) {
        std::vector<clang::Stmt *> new_else_body{};

        if (else_a) {
          new_else_body.push_back(else_a);
        }

        if (else_b) {
          new_else_body.push_back(else_b);
        }

        auto new_else{ast.CreateCompoundStmt(new_else_body)};
        new_if->setElse(new_else);
      }

      provenance.conds[new_if] = provenance.conds[if_a];
      new_body[i] = new_if;
      new_body.erase(std::next(new_body.begin(), i + 1));
      did_something = true;
      break;
    }

    if (Prove(provenance.z3_ctx, cond_a == !cond_b)) {
      std::vector<clang::Stmt *> new_then_body{then_a};
      if (else_b) {
        new_then_body.push_back(else_b);
      }

      auto new_then{ast.CreateCompoundStmt(new_then_body)};

      std::vector<clang::Stmt *> new_else_body{};
      if (else_a) {
        new_else_body.push_back(else_a);
      }
      new_else_body.push_back(then_b);

      auto new_if{ast.CreateIf(provenance.marker_expr, new_then)};

      auto new_else{ast.CreateCompoundStmt(new_else_body)};
      new_if->setElse(new_else);

      provenance.conds[new_if] = provenance.conds[if_a];
      new_body[i] = new_if;
      new_body.erase(std::next(new_body.begin(), i + 1));
      did_something = true;
      break;
    }
  }
  if (did_something) {
    auto new_compound{ast.CreateCompoundStmt(new_body)};
    substitutions[compound] = new_compound;
  }
  return !Stopped();
}

void CondBasedRefine::RunImpl() {
  LOG(INFO) << "Condition-based refinement";
  TransformVisitor<CondBasedRefine>::RunImpl();
  TraverseDecl(ast_ctx.getTranslationUnitDecl());
}

}  // namespace rellic