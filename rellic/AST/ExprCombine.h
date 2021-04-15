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
#include "rellic/AST/Util.h"

namespace rellic {

class ExprCombine : public llvm::ModulePass,
                    public TransformVisitor<ExprCombine> {
 private:
  clang::ASTUnit &unit;
  rellic::IRToASTVisitor *ast_gen;

 public:
  static char ID;

  ExprCombine(clang::ASTUnit &unit, rellic::IRToASTVisitor &ast_gen);

  bool VisitUnaryOperator(clang::UnaryOperator *op);
  bool VisitArraySubscriptExpr(clang::ArraySubscriptExpr *expr);
  bool VisitMemberExpr(clang::MemberExpr *expr);
  bool VisitParenExpr(clang::ParenExpr *paren);

  bool runOnModule(llvm::Module &module) override;
};

llvm::ModulePass *createExprCombinePass(clang::ASTUnit &unit,
                                        rellic::IRToASTVisitor &ast_gen);
}  // namespace rellic

namespace llvm {
void initializeExprCombinePass(PassRegistry &);
}
