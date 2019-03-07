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

#include <clang/AST/Mangle.h>

#include "rellic/AST/CXXToCDecl.h"
#include "rellic/AST/Util.h"

namespace rellic {

CXXToCDeclVisitor::CXXToCDeclVisitor(clang::ASTContext &ctx)
    : ast_ctx(ctx), c_tu(ctx.getTranslationUnitDecl()) {}

std::string CXXToCDeclVisitor::GetMangledName(clang::NamedDecl *decl) {
  auto mangler = decl->getASTContext().createMangleContext();
  std::string buffer;
  if (mangler->shouldMangleDeclName(decl)) {
    llvm::raw_string_ostream os(buffer);
    if (auto type_decl = clang::dyn_cast<clang::TypeDecl>(decl)) {
      auto type = clang::QualType(type_decl->getTypeForDecl(), 0);
      mangler->mangleTypeName(type, os);
    } else if (auto cst = clang::dyn_cast<clang::CXXConstructorDecl>(decl)) {
      mangler->mangleCXXCtor(cst, clang::Ctor_Complete, os);
    } else if (auto dst = clang::dyn_cast<clang::CXXDestructorDecl>(decl)) {
      mangler->mangleCXXDtor(dst, clang::Dtor_Complete, os);
    } else {
      mangler->mangleName(decl, os);
    }
    os.flush();
  }
  return buffer.empty() ? decl->getNameAsString() : buffer;
}

clang::RecordDecl *CXXToCDeclVisitor::GetOrCreateStructDecl(
    clang::CXXRecordDecl *cls) {
  auto &decl = c_decls[cls];
  if (!decl) {
    auto id = CreateIdentifier(ast_ctx, GetMangledName(cls));
    decl = CreateStructDecl(ast_ctx, c_tu, id);
  }
  return clang::cast<clang::RecordDecl>(decl);
}

clang::QualType CXXToCDeclVisitor::GetAsStructType(clang::QualType type) {
  auto type_decl = type->getAsCXXRecordDecl();
  // Nothing to do if we're not dealing with a class
  if (!type_decl) {
    return type;
  } 
  // Handle class-type attributes by translating to struct types
  auto struct_type = GetOrCreateStructDecl(type_decl)->getTypeForDecl();
  auto quals = type.getQualifiers().getAsOpaqueValue();
  return clang::QualType(struct_type, quals);
}

bool CXXToCDeclVisitor::VisitCXXMethodDecl(clang::CXXMethodDecl *method) {
  auto method_name = method->getNameAsString();
  DLOG(INFO) << "VisitCXXMethodDecl: " << method_name;
  // Check if the corresponding C function doesn't exist already
  if (c_decls.count(method)) {
    LOG(WARNING) << "Asking to re-generate method: " << method_name
                 << "; returning";
    return true;
  }
  // Create a forward decl of a C struct for this methods parent class
  auto struct_decl = GetOrCreateStructDecl(method->getParent());
  auto struct_type = struct_decl->getTypeForDecl();
  // Gather necessary types for the C function decl
  auto this_type = ast_ctx.getPointerType(clang::QualType(struct_type, 0));
  std::vector<clang::QualType> param_types({this_type});
  auto method_type = clang::cast<clang::FunctionProtoType>(method->getType());
  param_types.insert(param_types.end(), method_type->param_type_begin(),
                     method_type->param_type_end());
  // Create function prototype
  auto ret_type = GetAsStructType(method_type->getReturnType());
  auto func_type = ast_ctx.getFunctionType(ret_type, param_types,
                                           method_type->getExtProtoInfo());
  // Declare the C function
  auto func_id = CreateIdentifier(ast_ctx, GetMangledName(method));
  auto func_decl = CreateFunctionDecl(ast_ctx, c_tu, func_id, func_type);
  // Declare parameters
  auto this_id = CreateIdentifier(ast_ctx, "this");
  auto this_decl = CreateParmVarDecl(ast_ctx, func_decl, this_id, this_type);
  std::vector<clang::ParmVarDecl *> param_decls({this_decl});
  for (auto param : method->parameters()) {
    auto param_id = CreateIdentifier(ast_ctx, param->getNameAsString());
    auto param_type = GetAsStructType(param->getType());
    param_decls.push_back(
        CreateParmVarDecl(ast_ctx, func_decl, param_id, param_type));
  }
  // Set C function parameters
  func_decl->setParams(param_decls);
  // Save to C decl map
  c_decls[method] = func_decl;
  // Done
  return true;
}

bool CXXToCDeclVisitor::VisitCXXRecordDecl(clang::CXXRecordDecl *cls) {
  auto class_name = cls->getNameAsString();
  DLOG(INFO) << "VisitCXXRecordDecl: " << class_name;
  // Process only specializations of class templates
  if (cls->getDescribedClassTemplate()) {
    return true;
  }
  // Get a forward declaration
  auto struct_decl = GetOrCreateStructDecl(cls);
  if (cls->isPolymorphic()) {
    c_tu->addDecl(struct_decl);
    // Create a vtable
  }
  // Define the C structure
  auto struct_id = CreateIdentifier(ast_ctx, GetMangledName(cls));
  auto struct_defn = CreateStructDecl(ast_ctx, c_tu, struct_id, struct_decl);
  // Add attribute fields
  for (auto field : cls->fields()) {
    auto id = CreateIdentifier(ast_ctx, field->getName());
    auto type = GetAsStructType(field->getType());
    struct_defn->addDecl(CreateFieldDecl(ast_ctx, struct_defn, id, type));
  }
  // Complete the C structure definition
  struct_defn->completeDefinition();
  // Add the C structure to the C translation unit
  c_tu->addDecl(struct_defn);
  // Add methods to the C translation unit
  for (auto method : cls->methods()) {
    auto iter = c_decls.find(method);
    CHECK(iter != c_decls.end())
        << "C function declaration for " << method->getNameAsString()
        << " in class " << class_name << " does not exist";
    c_tu->addDecl(iter->second);
  }
  // Done
  return true;
}

}  // namespace rellic