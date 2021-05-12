/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/DeadStmtElim.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

namespace rellic {

char DeadStmtElim::ID = 0;

DeadStmtElim::DeadStmtElim(clang::ASTUnit &unit,
                           rellic::IRToASTVisitor &ast_gen)
    : ModulePass(DeadStmtElim::ID),
      ast(unit),
      ast_ctx(&unit.getASTContext()),
      ast_gen(&ast_gen) {}

bool DeadStmtElim::VisitIfStmt(clang::IfStmt *ifstmt) {
  // DLOG(INFO) << "VisitIfStmt";
  llvm::APSInt val;
  bool is_const = ifstmt->getCond()->isIntegerConstantExpr(val, *ast_ctx);
  auto compound = clang::dyn_cast<clang::CompoundStmt>(ifstmt->getThen());
  bool is_empty = compound ? compound->body_empty() : false;
  if ((is_const && !val.getBoolValue()) || is_empty) {
    substitutions[ifstmt] = nullptr;
  }
  return true;
}

bool DeadStmtElim::VisitCompoundStmt(clang::CompoundStmt *compound) {
  // DLOG(INFO) << "VisitCompoundStmt";
  std::vector<clang::Stmt *> new_body;
  for (auto stmt : compound->body()) {
    // Filter out nullptr statements
    if (!stmt) {
      continue;
    }
    // Add only necessary statements
    if (auto expr = clang::dyn_cast<clang::Expr>(stmt)) {
      if (expr->HasSideEffects(*ast_ctx)) {
        new_body.push_back(stmt);
      }
    } else {
      new_body.push_back(stmt);
    }
  }
  // Create the a new compound
  if (changed || new_body.size() < compound->size()) {
    substitutions[compound] = ast.CreateCompound(new_body);
  }
  return true;
}

bool DeadStmtElim::runOnModule(llvm::Module &module) {
  LOG(INFO) << "Eliminating dead statements";
  Initialize();
  TraverseDecl(ast_ctx->getTranslationUnitDecl());
  return changed;
}

llvm::ModulePass *createDeadStmtElimPass(clang::ASTUnit &unit,
                                         rellic::IRToASTVisitor &gen) {
  return new DeadStmtElim(unit, gen);
}
}  // namespace rellic