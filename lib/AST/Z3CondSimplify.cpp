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
    : TransformVisitor<Z3CondSimplify>(provenance, unit) {}

bool Z3CondSimplify::VisitIfStmt(clang::IfStmt *stmt) {
  auto cond{provenance.z3_exprs[provenance.conds[stmt]]};
  provenance.conds[stmt] = provenance.z3_exprs.size();
  provenance.z3_exprs.push_back(HeavySimplify(provenance.z3_ctx, cond));
  return true;
}

bool Z3CondSimplify::VisitWhileStmt(clang::WhileStmt *loop) {
  auto cond{provenance.z3_exprs[provenance.conds[loop]]};
  provenance.conds[loop] = provenance.z3_exprs.size();
  provenance.z3_exprs.push_back(HeavySimplify(provenance.z3_ctx, cond));
  return true;
}

bool Z3CondSimplify::VisitDoStmt(clang::DoStmt *loop) {
  auto cond{provenance.z3_exprs[provenance.conds[loop]]};
  provenance.conds[loop] = provenance.z3_exprs.size();
  provenance.z3_exprs.push_back(HeavySimplify(provenance.z3_ctx, cond));
  return true;
}

void Z3CondSimplify::RunImpl() {
  LOG(INFO) << "Simplifying conditions using Z3";
  TransformVisitor<Z3CondSimplify>::RunImpl();
  TraverseDecl(ast_ctx.getTranslationUnitDecl());
}

}  // namespace rellic
