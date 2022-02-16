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

/*
 * This pass propagates the condition of an outer if or while statement into the
 * conditions of its inner if statements. For example,
 *
 *   if(cond_a) {
 *     if(cond_a && cond_b) {
 *       body;
 *     }
 *   }
 *
 * turns into
 *
 *   if(cond_a) {
 *     if(1U && cond_b) {
 *       body;
 *     }
 *   }
 */
class NestedCondProp : public llvm::ModulePass,
                       public TransformVisitor<NestedCondProp> {
 private:
  ASTBuilder ast;
  clang::ASTContext *ast_ctx;

  std::unique_ptr<z3::context> z3_ctx;
  std::unique_ptr<rellic::Z3ConvVisitor> z3_gen;

  std::unordered_map<clang::Stmt *, clang::Expr *> parent_conds;

 public:
  static char ID;

  bool shouldTraversePostOrder() override { return false; }

  NestedCondProp(StmtToIRMap &provenance, clang::ASTUnit &unit);

  bool VisitIfStmt(clang::IfStmt *stmt);
  bool VisitWhileStmt(clang::WhileStmt *stmt);

  bool runOnModule(llvm::Module &module) override;
};

}  // namespace rellic
