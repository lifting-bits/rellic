/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/MaterializeConds.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Expr.h>
#include <clang/AST/OperationKinds.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

namespace rellic {

MaterializeConds::MaterializeConds(Provenance &provenance, clang::ASTUnit &unit)
    : TransformVisitor<MaterializeConds>(provenance, unit),
      ast_gen(unit, provenance) {}

bool MaterializeConds::VisitIfStmt(clang::IfStmt *stmt) {
  auto cond{provenance.z3_exprs[provenance.conds[stmt]]};
  if (stmt->getCond() == provenance.marker_expr) {
    stmt->setCond(ast_gen.ConvertExpr(cond));
  }
  return true;
}

bool MaterializeConds::VisitWhileStmt(clang::WhileStmt *stmt) {
  auto cond{provenance.z3_exprs[provenance.conds[stmt]]};
  if (stmt->getCond() == provenance.marker_expr) {
    stmt->setCond(ast_gen.ConvertExpr(cond));
  }
  return true;
}

bool MaterializeConds::VisitDoStmt(clang::DoStmt *stmt) {
  auto cond{provenance.z3_exprs[provenance.conds[stmt]]};
  if (stmt->getCond() == provenance.marker_expr) {
    stmt->setCond(ast_gen.ConvertExpr(cond));
  }
  return true;
}

void MaterializeConds::RunImpl() {
  LOG(INFO) << "Materializing conditions";
  TransformVisitor<MaterializeConds>::RunImpl();
  TraverseDecl(ast_ctx.getTranslationUnitDecl());
}

}  // namespace rellic
