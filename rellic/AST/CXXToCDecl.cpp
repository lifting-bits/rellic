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

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/CXXToCDecl.h"
#include "rellic/AST/Util.h"

namespace rellic {

CXXToCDeclVisitor::CXXToCDeclVisitor(clang::ASTContext *cxx,
                                     clang::ASTContext *c)
    : cxx_ast_ctx(cxx), c_ast_ctx(c) {}

bool CXXToCDeclVisitor::VisitCXXRecordDecl(clang::CXXRecordDecl *decl) {
  auto c_tu = c_ast_ctx->getTranslationUnitDecl();
  // Declare a structure for the CXX class
  auto c_struct = CreateStructDecl(*c_ast_ctx, c_tu, decl->getIdentifier());
  // Add fields to the C struct
  for (auto field : decl->fields()) {
    c_struct->addDecl(CreateFieldDecl(
        *c_ast_ctx, c_struct, field->getIdentifier(), field->getType()));
  }
  // Add the struct to the C translation unit
  c_struct->completeDefinition();
  c_tu->addDecl(c_struct);
  // Declare C functions for the CXX class methods
  for (auto method : decl->methods()) {
    auto c_func = CreateFunctionDecl(*c_ast_ctx, c_tu, method->getIdentifier(),
                                     method->getType());

    auto this_type = clang::QualType(c_struct->getTypeForDecl(), 0);
    auto this_param = CreateParmVarDecl(
        *c_ast_ctx, c_func, c_struct->getIdentifier(), this_type);

    std::vector<clang::ParmVarDecl *> params({this_param});
    for (auto param : method->parameters()) {
      params.push_back(CreateParmVarDecl(
          *c_ast_ctx, c_func, param->getIdentifier(), param->getType()));
    }

    c_func->setParams(params);
    c_tu->addDecl(c_func);
  }
  return true;
}

}  // namespace rellic