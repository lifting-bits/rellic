/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/Expr.h>
#include <llvm/IR/Module.h>
#include <llvm/Pass.h>

#include "rellic/AST/DebugInfoVisitor.h"
#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/TransformVisitor.h"

namespace rellic {

using DeclMap = std::unordered_map<clang::ValueDecl *, clang::ValueDecl *>;

class LocalDeclRenamer : public llvm::ModulePass,
                         public TransformVisitor<LocalDeclRenamer> {
 private:
  ASTBuilder ast;
  clang::ASTContext *ast_ctx;

  ValDeclToIRMap &decls;
  IRToNameMap &names;

 public:
  static char ID;

  LocalDeclRenamer(clang::ASTUnit &unit, IRToNameMap &names,
                   ValDeclToIRMap &decls);

  bool VisitDeclStmt(clang::DeclStmt *stmt);

  bool runOnModule(llvm::Module &module) override;
};

}  // namespace rellic