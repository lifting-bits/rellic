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
#include <clang/Frontend/ASTUnit.h>
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

namespace rellic {
class IRToASTVisitor {
 private:
  DecompilationContext &dec_ctx;
  ASTBuilder &ast;

 public:
  IRToASTVisitor(DecompilationContext &dec_ctx);

  clang::Expr *CreateOperandExpr(llvm::Use &val);
  clang::Expr *CreateConstantExpr(llvm::Constant *constant);
  clang::Expr *ConvertExpr(z3::expr expr);

  void VisitGlobalVar(llvm::GlobalVariable &var);
  void VisitFunctionDecl(llvm::Function &func);
  void VisitBasicBlock(llvm::BasicBlock &block,
                       std::vector<clang::Stmt *> &stmts);
};

}  // namespace rellic