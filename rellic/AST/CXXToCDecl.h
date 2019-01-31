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

namespace rellic {

class CXXToCDeclVisitor : public clang::RecursiveASTVisitor<CXXToCDeclVisitor> {
 private:
  clang::ASTContext *cxx_ast_ctx;
  clang::ASTContext *c_ast_ctx;

 public:
  CXXToCDeclVisitor(clang::ASTContext *cxx, clang::ASTContext *c);

  bool VisitCXXRecordDecl(clang::CXXRecordDecl *decl);
};

}  // namespace rellic

#endif  // RELLIC_AST_CXXTOCDECL_H_