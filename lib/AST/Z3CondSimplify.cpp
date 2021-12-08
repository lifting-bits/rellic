/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/Z3CondSimplify.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

namespace rellic {

char Z3CondSimplify::ID = 0;

Z3CondSimplify::Z3CondSimplify(clang::ASTUnit &unit)
    : ModulePass(Z3CondSimplify::ID),
      ast_ctx(&unit.getASTContext()),
      z_ctx(new z3::context()),
      z_gen(new rellic::Z3ConvVisitor(unit, z_ctx.get())),
      simplifier(*z_ctx, "simplify") {}

clang::Expr *Z3CondSimplify::SimplifyCExpr(clang::Expr *c_expr) {
  auto z_expr{z_gen->GetOrCreateZ3Expr(c_expr)};
  z3::goal goal(*z_ctx);
  goal.add(z_expr);
  // Apply on `simplifier` on condition
  auto app{simplifier(goal)};
  CHECK(app.size() == 1) << "Unexpected multiple goals in application!";
  auto z_result{app[0].as_expr()};
  return z_gen->GetOrCreateCExpr(z_result);
}

bool Z3CondSimplify::VisitIfStmt(clang::IfStmt *stmt) {
  stmt->setCond(SimplifyCExpr(stmt->getCond()));
  return true;
}

bool Z3CondSimplify::VisitWhileStmt(clang::WhileStmt *loop) {
  loop->setCond(SimplifyCExpr(loop->getCond()));
  return true;
}

bool Z3CondSimplify::VisitDoStmt(clang::DoStmt *loop) {
  loop->setCond(SimplifyCExpr(loop->getCond()));
  return true;
}

bool Z3CondSimplify::runOnModule(llvm::Module &module) {
  LOG(INFO) << "Simplifying conditions using Z3";
  Initialize();
  TraverseDecl(ast_ctx->getTranslationUnitDecl());
  return changed;
}

}  // namespace rellic