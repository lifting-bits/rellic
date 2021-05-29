/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <llvm/IR/Module.h>
#include <llvm/Pass.h>

#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/TransformVisitor.h"

namespace rellic {

class DeadStmtElim : public llvm::ModulePass,
                     public TransformVisitor<DeadStmtElim> {
 private:
  ASTBuilder ast;
  clang::ASTContext *ast_ctx;
  rellic::IRToASTVisitor *ast_gen;

 public:
  static char ID;

  DeadStmtElim(clang::ASTUnit &unit, rellic::IRToASTVisitor &ast_gen);

  bool VisitIfStmt(clang::IfStmt *ifstmt);
  bool VisitCompoundStmt(clang::CompoundStmt *compound);

  bool runOnModule(llvm::Module &module) override;
};

llvm::ModulePass *createDeadStmtElimPass(clang::ASTUnit &unit,
                                         rellic::IRToASTVisitor &ast_gen);
}  // namespace rellic

namespace llvm {
void initializeDeadStmtElimPass(PassRegistry &);
}
