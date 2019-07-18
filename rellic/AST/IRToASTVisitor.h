/*
 * Copyright (c) 2018 Trail of Bits, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/Operator.h>

#include <clang/AST/ASTContext.h>
#include <clang/Frontend/CompilerInstance.h>

#include <memory>
#include <unordered_map>

namespace rellic {

class IRToASTVisitor : public llvm::InstVisitor<IRToASTVisitor> {
 private:
  clang::ASTContext &ast_ctx;

  std::unordered_map<llvm::Type *, clang::TypeDecl *> type_decls;
  std::unordered_map<llvm::Value *, clang::ValueDecl *> value_decls;
  std::unordered_map<llvm::Value *, clang::Stmt *> stmts;

  clang::Expr *GetOperandExpr(llvm::Value *val);
  clang::QualType GetQualType(llvm::Type *type);

  clang::Expr *CreateLiteralExpr(llvm::Constant *constant);

  clang::VarDecl *CreateVarDecl(clang::DeclContext *decl_ctx, llvm::Type *type,
                                std::string name);

 public:
  IRToASTVisitor(clang::ASTContext &ctx);

  clang::Stmt *GetOrCreateStmt(llvm::Value *val);
  clang::Decl *GetOrCreateDecl(llvm::Value *val);

  void VisitGlobalVar(llvm::GlobalVariable &var);
  void VisitFunctionDecl(llvm::Function &func);
  void VisitArgument(llvm::Argument &arg);

  void visitCallInst(llvm::CallInst &inst);
  void visitGetElementPtrInst(llvm::GetElementPtrInst &inst);
  void visitAllocaInst(llvm::AllocaInst &inst);
  void visitLoadInst(llvm::LoadInst &inst);
  void visitStoreInst(llvm::StoreInst &inst);
  void visitReturnInst(llvm::ReturnInst &inst);
  void visitBinaryOperator(llvm::BinaryOperator &inst);
  void visitCmpInst(llvm::CmpInst &inst);
  void visitPtrToInt(llvm::PtrToIntInst &inst);
};

}  // namespace rellic