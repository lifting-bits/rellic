/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/RecursiveASTVisitor.h>
#include <llvm/IR/Module.h>
#include <llvm/Pass.h>
#include <z3++.h>

#include "rellic/AST/ASTBuilder.h"
#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/TransformVisitor.h"
#include "rellic/AST/Z3ConvVisitor.h"

namespace rellic {

class NestedCondProp : public llvm::ModulePass,
                       public TransformVisitor<NestedCondProp> {
 private:
  ASTBuilder ast;
  clang::ASTContext *ast_ctx;
  rellic::IRToASTVisitor *ast_gen;
  std::unique_ptr<z3::context> z3_ctx;
  std::unique_ptr<rellic::Z3ConvVisitor> z3_gen;

  std::unordered_map<clang::IfStmt *, clang::Expr *> parent_conds;

 public:
  static char ID;

  bool shouldTraversePostOrder() { return false; }

  NestedCondProp(clang::ASTUnit &unit, rellic::IRToASTVisitor &ast_gen);

  bool VisitIfStmt(clang::IfStmt *stmt);

  bool runOnModule(llvm::Module &module) override;
};

llvm::ModulePass *createNestedCondPropPass(clang::ASTUnit &unit,
                                           rellic::IRToASTVisitor &ast_gen);
}  // namespace rellic

namespace llvm {
void initializeNestedCondPropPass(PassRegistry &);
}
