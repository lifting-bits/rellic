/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/Stmt.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/Value.h>
#include <rellic/AST/Util.h>

#include <memory>
#include <unordered_map>

#include "rellic/AST/ASTBuilder.h"
#include "rellic/AST/Compat/ASTContext.h"

namespace rellic {

using IRToTypeDeclMap = std::unordered_map<llvm::Type *, clang::TypeDecl *>;
using IRToValDeclMap = std::unordered_map<llvm::Value *, clang::ValueDecl *>;
using IRToStmtMap = std::unordered_map<llvm::Value *, clang::Stmt *>;

class IRToASTVisitor : public llvm::InstVisitor<IRToASTVisitor> {
 private:
  clang::ASTContext &ast_ctx;

  ASTBuilder ast;

  IRToTypeDeclMap type_decls;
  IRToValDeclMap value_decls;
  IRToStmtMap stmts;

  clang::Expr *GetOperandExpr(llvm::Value *val);
  clang::QualType GetQualType(llvm::Type *type);

  clang::Expr *CreateLiteralExpr(llvm::Constant *constant);

  clang::Decl *GetOrCreateIntrinsic(llvm::InlineAsm *val);

 public:
  IRToASTVisitor(clang::ASTUnit &unit);

  clang::Stmt *GetOrCreateStmt(llvm::Value *val);
  clang::Decl *GetOrCreateDecl(llvm::Value *val);

  IRToStmtMap &GetIRToStmtMap() { return stmts; }

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