/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/ASTUnit.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include <unordered_map>

namespace rellic {

class ASTPrinter : public clang::RecursiveASTVisitor<ASTPrinter> {
 private:
  llvm::raw_ostream &os;
  clang::ASTUnit &unit;
  std::unordered_map<clang::Decl *, std::string> decl_strs;
  std::unordered_map<clang::Stmt *, std::string> stmt_strs;

  std::string print(clang::Decl *decl);
  std::string print(clang::Stmt *stmt);
  std::string print(clang::QualType type);

 public:
  ASTPrinter(llvm::raw_ostream &os, clang::ASTUnit &unit)
      : os(os), unit(unit) {}

  bool shouldTraversePostOrder() { return true; }

  bool WalkUpFromTranslationUnitDecl(clang::TranslationUnitDecl *tudecl) {
    return VisitTranslationUnitDecl(tudecl);
  }

  bool VisitTranslationUnitDecl(clang::TranslationUnitDecl *tudecl);

  bool WalkUpFromFunctionDecl(clang::FunctionDecl *fdecl) {
    return VisitFunctionDecl(fdecl);
  }

  bool VisitFunctionDecl(clang::FunctionDecl *fdecl);

  bool VisitDecl(clang::Decl *decl);

  bool WalkUpFromIntegerLiteral(clang::IntegerLiteral *ilit) {
    return VisitIntegerLiteral(ilit);
  }

  bool VisitIntegerLiteral(clang::IntegerLiteral *ilit);
};

}  // namespace rellic