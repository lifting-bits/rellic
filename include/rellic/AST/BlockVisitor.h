/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/ASTContext.h>

#include <vector>

#include "rellic/AST/ASTBuilder.h"
#include "rellic/AST/IRToASTVisitor.h"

namespace rellic {
// BlockVisitor is tasked with populating blocks with their top-level
// statements. It delegates to IRToASTVisitor for generating the expressions
// used by each statement.
//
// Most instructions in a LLVM BasicBlock are actually free of side effects and
// only make sense as part of expressions. The handful of instructions that
// actually have potential side effects are handled here, by creating
// top-level statements for them, and eventually even storing their result in a
// local variable if it was deemed necessary by
// IRToASTVisitor::VisitFunctionDecl
class BlockVisitor : public llvm::InstVisitor<BlockVisitor, clang::Stmt *> {
 private:
  clang::ASTContext &ast_ctx;
  ASTBuilder &ast;
  IRToASTVisitor &ast_gen;
  std::vector<clang::Stmt *> &stmts;
  Provenance &provenance;

 public:
  BlockVisitor(clang::ASTContext &ast_ctx, ASTBuilder &ast,
               IRToASTVisitor &ast_gen, std::vector<clang::Stmt *> &stmts,
               Provenance &provenance);

  clang::Stmt *visitStoreInst(llvm::StoreInst &inst);

  // Function calls have potential side effects: generate a top-level
  // statements. If a local variable was created by
  // IRToASTVisitor::VisitFunctionDecl, put the result of the call there.
  clang::Stmt *visitCallInst(llvm::CallInst &inst);

  // Returns only make sense as top-level statements.
  clang::Stmt *visitReturnInst(llvm::ReturnInst &inst);
  clang::Stmt *visitBranchInst(llvm::BranchInst &inst);
  clang::Stmt *visitUnreachableInst(llvm::UnreachableInst &inst);

  // Allocas are treated as local variable declarations by
  // IRToASTVisitor::VisitFunctionDecl
  clang::Stmt *visitAllocaInst(llvm::AllocaInst &inst) { return nullptr; }
  clang::Stmt *visitInstruction(llvm::Instruction &inst);
  void visitBasicBlock(llvm::BasicBlock &block);
};
}  // namespace rellic