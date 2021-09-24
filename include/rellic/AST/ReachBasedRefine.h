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
#include "rellic/AST/Z3ConvVisitor.h"

namespace rellic {

class ReachBasedRefine : public llvm::ModulePass,
                         public TransformVisitor<ReachBasedRefine> {
 private:
  ASTBuilder ast;
  clang::ASTContext *ast_ctx;

  std::unique_ptr<z3::context> z3_ctx;
  std::unique_ptr<rellic::Z3ConvVisitor> z3_gen;

  z3::tactic z3_solver;

  z3::expr GetZ3Cond(clang::IfStmt *ifstmt);

  bool Prove(z3::expr expr);

  using IfStmtVec = std::vector<clang::IfStmt *>;

  void CreateIfElseStmts(IfStmtVec stmts);

 public:
  static char ID;

  ReachBasedRefine(clang::ASTUnit &unit);

  bool VisitCompoundStmt(clang::CompoundStmt *compound);

  bool runOnModule(llvm::Module &module) override;
};

}  // namespace rellic