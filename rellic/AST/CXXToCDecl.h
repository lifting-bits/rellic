/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/RecursiveASTVisitor.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include <unordered_map>

#include "rellic/AST/ASTBuilder.h"

namespace clang {
class ASTUnit;
}

namespace rellic {

class CXXToCDeclVisitor : public clang::RecursiveASTVisitor<CXXToCDeclVisitor> {
 private:
  clang::ASTContext &ast_ctx;
  clang::TranslationUnitDecl *c_tu;

  ASTBuilder ast;

  std::unordered_map<clang::Decl *, clang::Decl *> c_decls;

  clang::QualType GetAsCType(clang::QualType type);

 public:
  CXXToCDeclVisitor(clang::ASTUnit &unit);

  bool shouldVisitTemplateInstantiations() { return true; }

  bool TraverseFunctionTemplateDecl(clang::FunctionTemplateDecl *decl) {
    // Ignore function templates
    return true;
  }

  bool TraverseClassTemplateDecl(clang::ClassTemplateDecl *decl) {
    // Only process class template specializations
    for (auto spec : decl->specializations()) {
      TraverseDecl(spec);
    }
    return true;
  }

  bool VisitFunctionDecl(clang::FunctionDecl *func);
  bool VisitCXXMethodDecl(clang::CXXMethodDecl *method);
  bool VisitRecordDecl(clang::RecordDecl *record);
  bool VisitCXXRecordDecl(clang::CXXRecordDecl *cls);
  bool VisitFieldDecl(clang::FieldDecl *field);
};

}  // namespace rellic