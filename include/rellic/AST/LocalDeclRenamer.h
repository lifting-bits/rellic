/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/ASTUnit.h>

#include <unordered_map>
#include <unordered_set>

#include "rellic/AST/DebugInfoCollector.h"
#include "rellic/AST/IRToASTVisitor.h"

namespace rellic {

using ValDeclToIRMap = std::unordered_map<clang::ValueDecl *, llvm::Value *>;

class LocalDeclRenamer : public clang::RecursiveASTVisitor<LocalDeclRenamer> {
 private:
  clang::ASTUnit &unit;
  clang::ASTContext &ast_ctx;
  ASTBuilder ast;
  ValDeclToIRMap decls;

  // Stores currently visible names, with scope awareness
  std::vector<std::unordered_set<std::string>> seen_names;

  std::unordered_set<clang::VarDecl *> renamed_decls;
  IRToNameMap &names;
  IRToValDeclMap &inv_decl;

  bool IsNameVisible(const std::string &name);

 public:
  LocalDeclRenamer(clang::ASTUnit &unit, IRToNameMap &names,
                   IRToValDeclMap &decls);

  bool shouldTraversePostOrder();
  bool VisitVarDecl(clang::VarDecl *decl);
  bool TraverseFunctionDecl(clang::FunctionDecl *decl);
};

}  // namespace rellic