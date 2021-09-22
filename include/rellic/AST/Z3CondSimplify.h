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

  std::unique_ptr<z3::context> z_ctx;
  std::unique_ptr<rellic::Z3ConvVisitor> z_gen;

  z3::tactic simplifier;

  clang::Expr *SimplifyCExpr(clang::Expr *c_expr);

 public:
  static char ID;

  Z3CondSimplify(clang::ASTUnit &unit);

  z3::context &GetZ3Context() { return *z_ctx; }

  void SetZ3Simplifier(z3::tactic tactic) { simplifier = tactic; };

  bool VisitIfStmt(clang::IfStmt *stmt);
  bool VisitWhileStmt(clang::WhileStmt *loop);
  bool VisitDoStmt(clang::DoStmt *loop);

  bool runOnModule(llvm::Module &module) override;
};

Z3CondSimplify *createZ3CondSimplifyPass(clang::ASTUnit &unit);
}  // namespace rellic
