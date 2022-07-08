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
    : TransformVisitor<CondBasedRefine>(provenance, unit) {}

void CondBasedRefine::CreateIfThenElseStmts(IfStmtVec worklist) {
  auto RemoveFromWorkList = [&worklist](clang::Stmt *stmt) {
    auto it = std::find(worklist.begin(), worklist.end(), stmt);
    if (it != worklist.end()) {
      worklist.erase(it);
    }
  };

  auto ThenTest = [this](z3::expr lhs, z3::expr rhs) {
    return Prove(provenance.z3_ctx, lhs == rhs);
  };

  auto ElseTest = [this](z3::expr lhs, z3::expr rhs) {
    return Prove(provenance.z3_ctx, lhs == !rhs);
  };

  auto CombineTest = [this](z3::expr lhs, z3::expr rhs) {
    return Prove(provenance.z3_ctx, z3::implies(rhs, lhs));
  };

  while (!worklist.empty()) {
    auto lhs = *worklist.begin();
    RemoveFromWorkList(lhs);
    // Prepare conditions according to which we're going to
    // cluster statements according to the whole `lhs`
    // condition.
    auto lcond = provenance.z3_exprs[provenance.conds[lhs]];
    // Get branch candidates wrt `clause`
    std::vector<clang::Stmt *> thens({lhs}), elses;
    for (auto rhs : worklist) {
      auto rcond = provenance.z3_exprs[provenance.conds[rhs]];
      if (ThenTest(lcond, rcond) || CombineTest(lcond, rcond)) {
        thens.push_back(rhs);
      } else if (ElseTest(lcond, rcond) || CombineTest(!lcond, rcond)) {
        elses.push_back(rhs);
      }
    }

    // Check if we have enough statements to work with
    if (thens.size() + elses.size() < 2) {
      continue;
    }

    // Erase then statements from the AST and `worklist`
    for (auto stmt : thens) {
      RemoveFromWorkList(stmt);
      substitutions[stmt] = nullptr;
    }
    // Create our new if-then
    auto sub =
        ast.CreateIf(provenance.marker_expr, ast.CreateCompoundStmt(thens));
    provenance.conds[sub] = provenance.z3_exprs.size();
    provenance.z3_exprs.push_back(lcond);
    // Create an else branch if possible
    if (!elses.empty()) {
      // Erase else statements from the AST and `worklist`
      for (auto stmt : elses) {
        RemoveFromWorkList(stmt);
        substitutions[stmt] = nullptr;
      }
    } else if (Prove(*z3_ctx, cond_a == !cond_b)) {
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