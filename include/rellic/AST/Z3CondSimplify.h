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

#include "rellic/AST/ASTBuilder.h"
#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/TransformVisitor.h"
#include "rellic/AST/Z3ExprSimplifier.h"

namespace rellic {

class Z3CondSimplify : public llvm::ModulePass,
                       public TransformVisitor<Z3CondSimplify> {
 private:
  clang::ASTContext *ast_ctx;
  std::unique_ptr<Z3ExprSimplifier> simplifier;

 public:
  static char ID;

  Z3CondSimplify(StmtToIRMap &provenance, clang::ASTUnit &unit);

  z3::context &GetZ3Context() { return simplifier->GetZ3Context(); }

  void SetZ3Tactic(z3::tactic tactic) { simplifier->SetZ3Tactic(tactic); };

  bool VisitIfStmt(clang::IfStmt *stmt);
  bool VisitWhileStmt(clang::WhileStmt *loop);
  bool VisitDoStmt(clang::DoStmt *loop);

  bool runOnModule(llvm::Module &module) override;
};

}  // namespace rellic
