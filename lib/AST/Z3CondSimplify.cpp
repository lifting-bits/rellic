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

char Z3CondSimplify::ID = 0;

Z3CondSimplify::Z3CondSimplify(StmtToIRMap &provenance, clang::ASTUnit &unit)
    : ModulePass(Z3CondSimplify::ID),
      TransformVisitor<Z3CondSimplify>(provenance),
      ast_ctx(&unit.getASTContext()),
      simplifier(new Z3ExprSimplifier(unit)) {}

bool Z3CondSimplify::VisitIfStmt(clang::IfStmt *stmt) {
  stmt->setCond(simplifier->Simplify(stmt->getCond()));
  return true;
}

bool Z3CondSimplify::VisitWhileStmt(clang::WhileStmt *loop) {
  loop->setCond(simplifier->Simplify(loop->getCond()));
  return true;
}

bool Z3CondSimplify::VisitDoStmt(clang::DoStmt *loop) {
  loop->setCond(simplifier->Simplify(loop->getCond()));
  return true;
}

bool Z3CondSimplify::runOnModule(llvm::Module &module) {
  LOG(INFO) << "Simplifying conditions using Z3";
  Initialize();
  TraverseDecl(ast_ctx->getTranslationUnitDecl());
  return changed;
}

}  // namespace rellic
