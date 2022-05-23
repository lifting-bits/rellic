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

namespace rellic {

namespace {

using IfStmtVec = std::vector<clang::IfStmt *>;

static IfStmtVec GetIfStmts(clang::CompoundStmt *compound) {
  IfStmtVec result;
  for (auto stmt : compound->body()) {
    if (auto ifstmt = clang::dyn_cast<clang::IfStmt>(stmt)) {
      result.push_back(ifstmt);
    }
  }
  return result;
}

}  // namespace

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

void CondBasedRefine::CreateIfThenElseStmts(IfStmtVec worklist) {
  auto RemoveFromWorkList = [&worklist](clang::Stmt *stmt) {
    auto it = std::find(worklist.begin(), worklist.end(), stmt);
    if (it != worklist.end()) {
      worklist.erase(it);
    }
  };

  auto ThenTest = [this](z3::expr lhs, z3::expr rhs) {
    return Prove(*z3_ctx, lhs == rhs);
  };

  auto ElseTest = [this](z3::expr lhs, z3::expr rhs) {
    return Prove(*z3_ctx, lhs == !rhs);
  };

  auto CombineTest = [this](z3::expr lhs, z3::expr rhs) {
    return Prove(*z3_ctx, z3::implies(rhs, lhs));
  };

  while (!worklist.empty()) {
    auto lhs = *worklist.begin();
    RemoveFromWorkList(lhs);
    // Prepare conditions according to which we're going to
    // cluster statements according to the whole `lhs`
    // condition.
    auto lcond = GetZ3Cond(lhs);
    // Get branch candidates wrt `clause`
    std::vector<clang::Stmt *> thens({lhs}), elses;
    for (auto rhs : worklist) {
      auto rcond = GetZ3Cond(rhs);
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
    auto sub = ast.CreateIf(lhs->getCond(), ast.CreateCompoundStmt(thens));
    // Create an else branch if possible
    if (!elses.empty()) {
      // Erase else statements from the AST and `worklist`
      for (auto stmt : elses) {
        RemoveFromWorkList(stmt);
        substitutions[stmt] = nullptr;
      }
      // Add the else branch
      sub->setElse(ast.CreateCompoundStmt(elses));
    }
    // Replace `lhs` with the new `sub`
    substitutions[lhs] = sub;
  }
}

bool CondBasedRefine::VisitCompoundStmt(clang::CompoundStmt *compound) {
  // DLOG(INFO) << "VisitCompoundStmt";
  // Create if-then-else substitutions for IfStmts in `compound`
  CreateIfThenElseStmts(GetIfStmts(compound));
  // Apply created if-then-else substitutions and
  // create a replacement for `compound`
  if (ReplaceChildren(compound, substitutions)) {
    std::vector<clang::Stmt *> new_body;
    for (auto stmt : compound->body()) {
      if (stmt) {
        new_body.push_back(stmt);
      }
    }
    substitutions[compound] = ast.CreateCompoundStmt(new_body);
  }
  return !Stopped();
}

void CondBasedRefine::RunImpl() {
  LOG(INFO) << "Condition-based refinement";
  TransformVisitor<CondBasedRefine>::RunImpl();
  TraverseDecl(ast_ctx.getTranslationUnitDecl());
}

}  // namespace rellic