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

#include <z3++.h>

#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/TransformVisitor.h"
#include "rellic/AST/Z3ConvVisitor.h"

namespace rellic {

class Z3CondSimplify : public llvm::ModulePass,
                       public TransformVisitor<Z3CondSimplify> {
 private:
  clang::ASTContext *ast_ctx;
  rellic::IRToASTVisitor *ast_gen;
  std::unique_ptr<z3::context> z3_ctx;
  std::unique_ptr<rellic::Z3ConvVisitor> z3_gen;

  z3::tactic z3_simplifier;

  clang::Expr *SimplifyCExpr(clang::Expr *c_expr);

 public:
  static char ID;

  Z3CondSimplify(clang::ASTContext &ctx, rellic::IRToASTVisitor &ast_gen);

  z3::context &GetZ3Context() { return *z3_ctx; }
  
  void SetZ3Simplifier(z3::tactic tactic) { z3_simplifier = tactic; };

  bool VisitIfStmt(clang::IfStmt *stmt);
  bool VisitWhileStmt(clang::WhileStmt *loop);
  bool VisitDoStmt(clang::DoStmt *loop);

  bool runOnModule(llvm::Module &module) override;
};

llvm::ModulePass *createZ3CondSimplifyPass(clang::ASTContext &ctx,
                                           rellic::IRToASTVisitor &gen);
}  // namespace rellic

namespace llvm {
void initializeZ3CondSimplifyPass(PassRegistry &);
}
