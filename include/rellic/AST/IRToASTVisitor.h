/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/Operator.h>

#include <memory>
#include <unordered_map>

#include "rellic/AST/Compat/ASTContext.h"
#include "rellic/AST/ASTBuilder.h"

namespace rellic {

class IRToASTVisitor : public llvm::InstVisitor<IRToASTVisitor> {
 private:
  clang::ASTContext &ast_ctx;

  ASTBuilder ast;

  std::unordered_map<llvm::Type *, clang::TypeDecl *> type_decls;
  std::unordered_map<llvm::Value *, clang::ValueDecl *> value_decls;
  std::unordered_map<llvm::Value *, clang::Stmt *> stmts;

  clang::Expr *GetOperandExpr(llvm::Value *val);
  clang::QualType GetQualType(llvm::Type *type);

  clang::Expr *CreateLiteralExpr(llvm::Constant *constant);

  clang::Decl *GetOrCreateIntrinsic(llvm::InlineAsm *val);

 public:
  IRToASTVisitor(clang::ASTUnit &unit);

  clang::Stmt *GetOrCreateStmt(llvm::Value *val);
  clang::Decl *GetOrCreateDecl(llvm::Value *val);

  void SetStmt(llvm::Value *val, clang::Stmt *stmt);

  void VisitGlobalVar(llvm::GlobalVariable &var);
  void VisitFunctionDecl(llvm::Function &func);
  void VisitArgument(llvm::Argument &arg);

  void visitIntrinsicInst(llvm::IntrinsicInst &inst);
  void visitCallInst(llvm::CallInst &inst);
  void visitGetElementPtrInst(llvm::GetElementPtrInst &inst);
  void visitExtractValueInst(llvm::ExtractValueInst &inst);
  void visitAllocaInst(llvm::AllocaInst &inst);
  void visitLoadInst(llvm::LoadInst &inst);
  void visitStoreInst(llvm::StoreInst &inst);
  void visitReturnInst(llvm::ReturnInst &inst);
  void visitBinaryOperator(llvm::BinaryOperator &inst);
  void visitCmpInst(llvm::CmpInst &inst);
  void visitCastInst(llvm::CastInst &inst);
  void visitSelectInst(llvm::SelectInst &inst);
  void visitFreezeInst(llvm::FreezeInst &inst);
  void visitPHINode(llvm::PHINode &inst);
};

}  // namespace rellic