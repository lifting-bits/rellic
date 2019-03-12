/*
 * Copyright (c) 2019 Trail of Bits, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef RELLIC_AST_CXXTOCDECL_H_
#define RELLIC_AST_CXXTOCDECL_H_

#include <clang/AST/RecursiveASTVisitor.h>

#include <unordered_map>

namespace rellic {

class CXXToCDeclVisitor : public clang::RecursiveASTVisitor<CXXToCDeclVisitor> {
 private:
  clang::ASTContext &ast_ctx;
  clang::TranslationUnitDecl *c_tu;

  std::unordered_map<clang::Decl *, clang::Decl *> c_decls;

  std::string GetMangledName(clang::NamedDecl *decl);
  clang::QualType GetAsCType(clang::QualType type);
  

 public:
  CXXToCDeclVisitor(clang::ASTContext &ctx);

  bool shouldVisitTemplateInstantiations() { return true; }

  bool VisitFieldDecl(clang::FieldDecl *field);
  bool VisitRecordDecl(clang::RecordDecl *record);
  bool VisitParmVarDecl(clang::ParmVarDecl *param);
  bool VisitCXXRecordDecl(clang::CXXRecordDecl *cls);
  bool VisitCXXMethodDecl(clang::CXXMethodDecl *method);
};

}  // namespace rellic

#endif  // RELLIC_AST_CXXTOCDECL_H_