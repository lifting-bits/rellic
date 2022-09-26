/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/MaterializeConds.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/Util.h"

namespace rellic {

MaterializeConds::MaterializeConds(DecompilationContext &dec_ctx)
    : TransformVisitor<MaterializeConds>(dec_ctx), ast_gen(dec_ctx) {}

bool MaterializeConds::VisitIfStmt(clang::IfStmt *stmt) {
  auto cond{dec_ctx.z3_exprs[dec_ctx.conds[stmt]]};
  if (stmt->getCond() == dec_ctx.marker_expr) {
    stmt->setCond(ast_gen.ConvertExpr(cond));
  }
  return true;
}

bool MaterializeConds::VisitWhileStmt(clang::WhileStmt *stmt) {
  auto cond{dec_ctx.z3_exprs[dec_ctx.conds[stmt]]};
  if (stmt->getCond() == dec_ctx.marker_expr) {
    stmt->setCond(ast_gen.ConvertExpr(cond));
  }
  return true;
}

bool MaterializeConds::VisitDoStmt(clang::DoStmt *stmt) {
  auto cond{dec_ctx.z3_exprs[dec_ctx.conds[stmt]]};
  if (stmt->getCond() == dec_ctx.marker_expr) {
    stmt->setCond(ast_gen.ConvertExpr(cond));
  }
  return true;
}

void MaterializeConds::RunImpl() {
  LOG(INFO) << "Materializing conditions";
  TransformVisitor<MaterializeConds>::RunImpl();
  TraverseDecl(dec_ctx.ast_ctx.getTranslationUnitDecl());
}

}  // namespace rellic
