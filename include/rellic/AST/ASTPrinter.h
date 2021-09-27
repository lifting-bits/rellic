/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclVisitor.h>
#include <clang/AST/Stmt.h>
#include <clang/Frontend/ASTUnit.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

namespace rellic {

struct Token {
  union ASTNodeRef {
    clang::Stmt *stmt;
    clang::Type *type;
    clang::Decl *decl;
  } node;

  std::string str;
};

class DeclTokenizer : public clang::DeclVisitor<DeclTokenizer> {
 private:
  std::list<Token> &out;
  const clang::ASTUnit &unit;

  void PrintGroup(clang::Decl **begin, unsigned num_decls);
  void ProcessDeclGroup(llvm::SmallVectorImpl<clang::Decl *> &decls);

 public:
  DeclTokenizer(std::list<Token> &out, const clang::ASTUnit &unit)
      : out(out), unit(unit) {}

  void VisitDeclContext(clang::DeclContext *dctx);
  void VisitTranslationUnitDecl(clang::TranslationUnitDecl *decl);
};

}  // namespace rellic