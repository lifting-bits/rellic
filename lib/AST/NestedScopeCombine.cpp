/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/NestedScopeCombine.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/Util.h"

namespace rellic {

NestedScopeCombine::NestedScopeCombine(DecompilationContext &dec_ctx,
                                       clang::ASTUnit &unit)
    : TransformVisitor<NestedScopeCombine>(dec_ctx, unit) {}

bool NestedScopeCombine::VisitIfStmt(clang::IfStmt *ifstmt) {
  // DLOG(INFO) << "VisitIfStmt";
  // Determine whether `cond` is a constant expression that is always true and
  // `ifstmt` should be replaced by `then` in it's parent nodes.
  auto cond{dec_ctx.z3_exprs[dec_ctx.conds[ifstmt]]};
  if (Prove(cond)) {
    substitutions[ifstmt] = ifstmt->getThen();
  } else if (Prove(!cond) && ifstmt->getElse()) {
    substitutions[ifstmt] = ifstmt->getElse();
  }
  return !Stopped();
}

bool NestedScopeCombine::VisitWhileStmt(clang::WhileStmt *stmt) {
  auto cond{dec_ctx.z3_exprs[dec_ctx.conds[stmt]]};
  if (Prove(cond)) {
    auto body{clang::cast<clang::CompoundStmt>(stmt->getBody())};
    if (clang::isa<clang::BreakStmt>(body->body_back())) {
      std::vector<clang::Stmt *> new_body{body->body_begin(),
                                          body->body_end() - 1};
      substitutions[stmt] = ast.CreateCompoundStmt(new_body);
    }
  }
  return !Stopped();
}

bool NestedScopeCombine::VisitCompoundStmt(clang::CompoundStmt *compound) {
  // DLOG(INFO) << "VisitCompoundStmt";
  bool has_compound = false;
  std::vector<clang::Stmt *> new_body;
  for (auto stmt : compound->body()) {
    if (auto child = clang::dyn_cast<clang::CompoundStmt>(stmt)) {
      new_body.insert(new_body.end(), child->body_begin(), child->body_end());
      has_compound = true;
    } else {
      new_body.push_back(stmt);
    }
  }

  if (has_compound) {
    substitutions[compound] = ast.CreateCompoundStmt(new_body);
  }

  return !Stopped();
}

void NestedScopeCombine::RunImpl() {
  LOG(INFO) << "Combining nested scopes";
  TransformVisitor<NestedScopeCombine>::RunImpl();
  TraverseDecl(ast_ctx.getTranslationUnitDecl());
}

}  // namespace rellic
