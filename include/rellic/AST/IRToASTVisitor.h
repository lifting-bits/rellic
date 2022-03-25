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

class IRToASTVisitor : public llvm::InstVisitor<IRToASTVisitor, clang::Expr *> {
 private:
  clang::ASTContext &ast_ctx;

  ASTBuilder ast;

  IRToTypeDeclMap &type_decls;
  IRToValDeclMap &value_decls;
  ArgToTempMap &temp_decls;
  ExprToUseMap &use_provenance;
  size_t num_literal_structs = 0;
  size_t num_declared_structs = 0;

  clang::QualType GetQualType(llvm::Type *type);

  clang::Expr *CreateConstantExpr(llvm::Constant *constant);
  clang::Expr *CreateLiteralExpr(llvm::Constant *constant);

  clang::Decl *GetOrCreateIntrinsic(llvm::InlineAsm *val);

 public:
  IRToASTVisitor(clang::ASTUnit &unit, IRToTypeDeclMap &type_decls,
                 IRToValDeclMap &value_decls, ArgToTempMap &temp_decls,
                 ExprToUseMap &use_provenance);

  clang::Decl *GetOrCreateDecl(llvm::Value *val);
  clang::Expr *GetOperandExpr(llvm::Use &val);

  ExprToUseMap &GetExprToUseMap() { return use_provenance; }
  IRToValDeclMap &GetIRToValDeclMap() { return value_decls; }
  IRToTypeDeclMap &GetIRToTypeDeclMap() { return type_decls; }

  void VisitGlobalVar(llvm::GlobalVariable &var);
  void VisitFunctionDecl(llvm::Function &func);
  void VisitArgument(llvm::Argument &arg);

  clang::Expr *visitMemCpyInst(llvm::MemCpyInst &inst);
  clang::Expr *visitMemCpyInlineInst(llvm::MemCpyInlineInst &inst);
  clang::Expr *visitAnyMemMoveInst(llvm::AnyMemMoveInst &inst);
  clang::Expr *visitAnyMemSetInst(llvm::AnyMemSetInst &inst);
  clang::Expr *visitIntrinsicInst(llvm::IntrinsicInst &inst);
  clang::Expr *visitCallInst(llvm::CallInst &inst);
  clang::Expr *visitGetElementPtrInst(llvm::GetElementPtrInst &inst);
  clang::Expr *visitExtractValueInst(llvm::ExtractValueInst &inst);
  clang::Expr *visitLoadInst(llvm::LoadInst &inst);
  clang::Expr *visitBinaryOperator(llvm::BinaryOperator &inst);
  clang::Expr *visitCmpInst(llvm::CmpInst &inst);
  clang::Expr *visitCastInst(llvm::CastInst &inst);
  clang::Expr *visitSelectInst(llvm::SelectInst &inst);
  clang::Expr *visitFreezeInst(llvm::FreezeInst &inst);
  clang::Expr *visitPHINode(llvm::PHINode &inst);
  clang::Expr *visitInstruction(llvm::Instruction &inst);
};

}  // namespace rellic