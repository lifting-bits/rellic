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

namespace rellic {

/*
 * This pass combines the bodies of trivially true if statements and compound
 * statements into the body of their parent's body, and deletes trivially false
 * if statements. For example,
 *
 *   {
 *     if(1U) {
 *       body1;
 *     }
 *     if(0U) {
 *       body2;
 *     }
 *   }
 *
 * becomes
 *
 *   body1;
 */
class NestedScopeCombine : public llvm::ModulePass,
                           public TransformVisitor<NestedScopeCombine> {
 private:
  ASTBuilder ast;
  clang::ASTContext *ast_ctx;

 public:
  static char ID;

  NestedScopeCombine(StmtToIRMap &provenance, clang::ASTUnit &unit);

  bool VisitIfStmt(clang::IfStmt *ifstmt);
  bool VisitCompoundStmt(clang::CompoundStmt *compound);

  bool runOnModule(llvm::Module &module) override;
};

}  // namespace rellic
