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

clang::QualType CXXToCDeclVisitor::GetAsCType(clang::QualType type) {
  auto cls = type->getAsCXXRecordDecl();
  // Nothing to do if we're not dealing with a class
  if (!cls) {
    return type;
  }
  // Handle class-type attributes by translating to struct types
  auto iter = c_decls.find(cls);
  CHECK(iter != c_decls.end())
      << "C struct for class" << cls->getNameAsString() << " does not exist";
  auto decl = clang::cast<clang::RecordDecl>(iter->second);
  auto quals = type.getQualifiers().getAsOpaqueValue();
  return clang::QualType(decl->getTypeForDecl(), quals);
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
  // Process only template specializations
  if (method->getDescribedFunctionTemplate()) {
    LOG(WARNING) << "Asking to generate from template; returning";
    return true;
  }
  auto iter = c_decls.find(method->getParent());
  if (iter == c_decls.end()) {
    LOG(WARNING) << "Parent of " << method_name
                 << " was not processed; returning";
    return true;
  }
  // Get the this pointer type
  auto struct_decl = clang::cast<clang::RecordDecl>(iter->second);
  auto struct_type = struct_decl->getTypeForDecl();
  auto this_type = ast_ctx.getPointerType(clang::QualType(struct_type, 0));
  // Gather parameter types
  std::vector<clang::QualType> param_types({this_type});
  auto method_type = clang::cast<clang::FunctionProtoType>(method->getType());
  param_types.insert(param_types.end(), method_type->param_type_begin(),
                     method_type->param_type_end());
  // Create function prototype
  auto ret_type = GetAsCType(method_type->getReturnType());
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
    auto param_type = GetAsCType(param->getType());
    param_decls.push_back(
        CreateParmVarDecl(ast_ctx, func_decl, param_id, param_type));
  }
  // Set C function parameters
  func_decl->setParams(param_decls);
  // Save to C translation unit
  c_tu->addDecl(func_decl);
  // Done
  return true;
}

bool CXXToCDeclVisitor::VisitFieldDecl(clang::FieldDecl *field) {
  auto name = field->getNameAsString();
  DLOG(INFO) << "FieldDecl: " << name;
  auto iter = c_decls.find(field->getParent());
  if (iter == c_decls.end()) {
    LOG(WARNING) << "C field " << name << " does not have a parent; returning";
    return true;
  }
  auto &decl = c_decls[field];
  if (!decl) {
    auto parent = clang::cast<clang::RecordDecl>(iter->second);
    auto id = CreateIdentifier(ast_ctx, field->getName());
    auto type = GetAsCType(field->getType());
    decl = CreateFieldDecl(ast_ctx, parent, id, type);
    parent->addDecl(decl);
  }
  return true;
}

bool CXXToCDeclVisitor::VisitCXXRecordDecl(clang::CXXRecordDecl *cls) {
  auto name = cls->getNameAsString();
  DLOG(INFO) << "VisitCXXRecordDecl: " << name;
  // Process only templates specializations
  if (cls->getDescribedClassTemplate()) {
    LOG(WARNING) << "Asking to generate from template; returning";
    return true;
  }
  auto &decl = c_decls[cls];
  if (!decl) {
    // Create a vtable
    if (cls->isPolymorphic()) {
    }
    auto id = CreateIdentifier(ast_ctx, GetMangledName(cls));
    decl = CreateStructDecl(ast_ctx, c_tu, id);
    // Complete the C structure definition
    clang::cast<clang::RecordDecl>(decl)->completeDefinition();
    // Add the C structure to the C translation unit
    c_tu->addDecl(decl);
  }
  // Done
  return true;
}

bool CXXToCDeclVisitor::VisitParmVarDecl(clang::ParmVarDecl *param) {
  auto name = param->getNameAsString();
  DLOG(INFO) << "VisitParmVarDecl: " << name;
  return true;
}

bool CXXToCDeclVisitor::VisitRecordDecl(clang::RecordDecl *record) {
  auto name = record->getNameAsString();
  DLOG(INFO) << "VisitRecordDecl: " << name;
  return true;
}

}  // namespace rellic