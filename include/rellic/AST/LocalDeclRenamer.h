/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <llvm/Pass.h>

#include <unordered_map>
#include <unordered_set>

#include "rellic/AST/DebugInfoCollector.h"
#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/TransformVisitor.h"

namespace rellic {

using ValDeclToIRMap = std::unordered_map<clang::ValueDecl *, llvm::Value *>;

class LocalDeclRenamer : public llvm::ModulePass,
                         public TransformVisitor<LocalDeclRenamer> {
 private:
  ASTBuilder ast;
  clang::ASTContext *ast_ctx;

  ValDeclToIRMap decls;

  // Stores currently visible names, with scope awareness
  std::vector<std::unordered_set<std::string>> seen_names;

  std::unordered_set<clang::VarDecl *> renamed_decls;
  IRToNameMap &names;
  IRToValDeclMap &inv_decl;

  bool IsNameVisible(const std::string &name);

 public:
  static char ID;

  LocalDeclRenamer(clang::ASTUnit &unit, IRToNameMap &names,
                   IRToValDeclMap &decls);

  bool shouldTraversePostOrder() override;
  bool VisitVarDecl(clang::VarDecl *decl);
  bool TraverseFunctionDecl(clang::FunctionDecl *decl);

  bool runOnModule(llvm::Module &module) override;
};

}  // namespace rellic