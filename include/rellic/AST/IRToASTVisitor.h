/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/Decl.h>
#include <clang/AST/Stmt.h>
#include <llvm/IR/Argument.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/IntrinsicInst.h>
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
using ArgToTempMap = std::unordered_map<llvm::Argument *, clang::VarDecl *>;

class IRToASTVisitor : public llvm::InstVisitor<IRToASTVisitor, clang::Stmt *> {
 private:
  clang::ASTContext &ast_ctx;

  ASTBuilder ast;

  IRToTypeDeclMap &type_decls;
  IRToValDeclMap &value_decls;
  StmtToIRMap &provenance;
  ArgToTempMap &temp_decls;
  size_t num_literal_structs = 0;
  size_t num_declared_structs = 0;

  clang::Expr *GetOperandExpr(llvm::Value *val);
  clang::QualType GetQualType(llvm::Type *type);

  clang::Expr *CreateLiteralExpr(llvm::Constant *constant);

  clang::Decl *GetOrCreateIntrinsic(llvm::InlineAsm *val);

 public:
  IRToASTVisitor(StmtToIRMap &provenance, clang::ASTUnit &unit,
                 IRToTypeDeclMap &type_decls, IRToValDeclMap &value_decls,
                 IRToStmtMap &stmts, ArgToTempMap &temp_decls);

  clang::Stmt *GetOrCreateStmt(llvm::Value *val);
  clang::Decl *GetOrCreateDecl(llvm::Value *val);

  StmtToIRMap &GetStmtToIRMap() { return provenance; }
  IRToValDeclMap &GetIRToValDeclMap() { return value_decls; }
  IRToTypeDeclMap &GetIRToTypeDeclMap() { return type_decls; }

  void VisitGlobalVar(llvm::GlobalVariable &var);
  void VisitFunctionDecl(llvm::Function &func);
  void VisitArgument(llvm::Argument &arg);

  clang::Stmt *visitMemCpyInst(llvm::MemCpyInst &inst);
  clang::Stmt *visitMemCpyInlineInst(llvm::MemCpyInlineInst &inst);
  clang::Stmt *visitAnyMemMoveInst(llvm::AnyMemMoveInst &inst);
  clang::Stmt *visitAnyMemSetInst(llvm::AnyMemSetInst &inst);
  clang::Stmt *visitIntrinsicInst(llvm::IntrinsicInst &inst);
  clang::Stmt *visitCallInst(llvm::CallInst &inst);
  clang::Stmt *visitGetElementPtrInst(llvm::GetElementPtrInst &inst);
  clang::Stmt *visitExtractValueInst(llvm::ExtractValueInst &inst);
  clang::Stmt *visitAllocaInst(llvm::AllocaInst &inst);
  clang::Stmt *visitLoadInst(llvm::LoadInst &inst);
  clang::Stmt *visitStoreInst(llvm::StoreInst &inst);
  clang::Stmt *visitReturnInst(llvm::ReturnInst &inst);
  clang::Stmt *visitBinaryOperator(llvm::BinaryOperator &inst);
  clang::Stmt *visitCmpInst(llvm::CmpInst &inst);
  clang::Stmt *visitCastInst(llvm::CastInst &inst);
  clang::Stmt *visitSelectInst(llvm::SelectInst &inst);
  clang::Stmt *visitFreezeInst(llvm::FreezeInst &inst);
  clang::Stmt *visitPHINode(llvm::PHINode &inst);
  clang::Stmt *visitBranchInst(llvm::BranchInst &inst);
  clang::Stmt *visitUnreachableInst(llvm::UnreachableInst &inst);
  clang::Stmt *visitInstruction(llvm::Instruction &inst);
};

}  // namespace rellic