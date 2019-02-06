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

CXXToCDeclVisitor::CXXToCDeclVisitor(clang::ASTContext *ctx) : ast_ctx(ctx) {}

bool CXXToCDeclVisitor::VisitCXXRecordDecl(clang::CXXRecordDecl *decl) {
  auto tu = ast_ctx->getTranslationUnitDecl();
  // Forward declare a C structure for the CXX class
  auto struct_dec = CreateStructDecl(*ast_ctx, tu, decl->getIdentifier());
  // Add the forward declaration to the C translation unit
  tu->addDecl(struct_dec);
  // Prepare a `this` pointer type for the C struct
  auto this_type =
      ast_ctx->getPointerType(clang::QualType(struct_dec->getTypeForDecl(), 0));
  // Declare C functions for the CXX class methods
  std::vector<clang::FunctionDecl *> method_funcs;
  for (auto method : decl->methods()) {
    // Gather parameter types
    std::vector<clang::QualType> param_types({this_type});
    auto method_type = llvm::cast<clang::FunctionProtoType>(method->getType());
    param_types.insert(param_types.end(), method_type->param_type_begin(),
                       method_type->param_type_end());
    // Create a C function prototype
    auto func_type =
        ast_ctx->getFunctionType(method_type->getReturnType(), param_types,
                                 method_type->getExtProtoInfo());
    // Create the C function declaration
    auto func =
        CreateFunctionDecl(*ast_ctx, tu, method->getIdentifier(), func_type);
    method_funcs.push_back(func);
    // Create parameter declarations
    auto this_param = CreateParmVarDecl(
        *ast_ctx, func, CreateIdentifier(*ast_ctx, "this"), this_type);
    std::vector<clang::ParmVarDecl *> params({this_param});
    for (auto param : method->parameters()) {
      params.push_back(CreateParmVarDecl(*ast_ctx, func, param->getIdentifier(),
                                         param->getType()));
    }
    // Add them to the C function declaration
    func->setParams(params);
    // Add the C function to the C translation unit
    tu->addDecl(func);
  }
  // Define the C structure
  auto struct_def =
      CreateStructDecl(*ast_ctx, tu, decl->getIdentifier(), struct_dec);
  // Add attribute fields
  for (auto field : decl->fields()) {
    struct_def->addDecl(CreateFieldDecl(
        *ast_ctx, struct_def, field->getIdentifier(), field->getType()));
  }
  // Add method fields
  for (auto func : method_funcs) {
    auto func_ptr = ast_ctx->getPointerType(func->getType());
    struct_def->addDecl(
        CreateFieldDecl(*ast_ctx, struct_def, func->getIdentifier(), func_ptr));
  }
  // Complete the C structure definition
  struct_def->completeDefinition();
  // Add the C structure to the C translation unit
  tu->addDecl(struct_def);
  // Done !
  return true;
}

}  // namespace rellic