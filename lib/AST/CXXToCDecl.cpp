/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/CXXToCDecl.h"

#include <clang/AST/Mangle.h>
#include <clang/AST/Type.h>
#include <clang/Frontend/ASTUnit.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

namespace rellic {

namespace {

static std::string GetMangledName(clang::NamedDecl *decl) {
  auto mangler = decl->getASTContext().createMangleContext();
  std::string buffer;
  if (mangler->shouldMangleDeclName(decl)) {
    llvm::raw_string_ostream os(buffer);
    if (auto type_decl = clang::dyn_cast<clang::TypeDecl>(decl)) {
      auto type = clang::QualType(type_decl->getTypeForDecl(), 0);
      mangler->mangleTypeName(type, os);
    } else if (auto cst = clang::dyn_cast<clang::CXXConstructorDecl>(decl)) {
      mangler->mangleName(clang::GlobalDecl(cst), os);
    } else if (auto dst = clang::dyn_cast<clang::CXXDestructorDecl>(decl)) {
      mangler->mangleName(clang::GlobalDecl(dst), os);
    } else {
      mangler->mangleName(decl, os);
    }
    os.flush();
  }
  return buffer.empty() ? decl->getNameAsString() : buffer;
}

}  // namespace

CXXToCDeclVisitor::CXXToCDeclVisitor(clang::ASTUnit &unit)
    : ast_ctx(unit.getASTContext()),
      c_tu(ast_ctx.getTranslationUnitDecl()),
      ast(unit) {}

clang::QualType CXXToCDeclVisitor::GetAsCType(clang::QualType type) {
  const clang::Type *result;
  if (auto ptr = type->getAs<clang::PointerType>()) {
    // Get a C pointer equivalent
    auto pointee = GetAsCType(ptr->getPointeeType());
    result = ast_ctx.getPointerType(pointee).getTypePtr();
  } else if (auto ref = type->getAs<clang::ReferenceType>()) {
    // Get a C pointer equivalent
    auto pointee = GetAsCType(ref->getPointeeType());
    auto ptr = ast_ctx.getPointerType(pointee);
    // Add `_Nonnull` attribute
    auto attr_type =
        ast_ctx.getAttributedType(clang::attr::TypeNonNull, ptr, ptr);
    result = attr_type.getTypePtr();
  } else if (auto cls = type->getAsCXXRecordDecl()) {
    // Handle class-type attributes by translating to struct types
    auto iter = c_decls.find(cls);
    CHECK(iter != c_decls.end())
        << "C struct for class" << cls->getNameAsString() << " does not exist";
    auto decl = clang::cast<clang::RecordDecl>(iter->second);
    result = decl->getTypeForDecl();
  } else {
    result = type.getTypePtr();
  }
  return clang::QualType(result, type.getQualifiers().getAsOpaqueValue());
}

bool CXXToCDeclVisitor::VisitFunctionDecl(clang::FunctionDecl *cxx_func) {
  auto name = cxx_func->getNameAsString();
  DLOG(INFO) << "VisitFunctionDecl: " << name;
  // Check if the corresponding C function doesn't exist already
  if (c_decls.count(cxx_func)) {
    LOG(WARNING) << "Asking to re-generate function: " << name << "; returning";
    return true;
  }
  // Gather parameter types
  auto cxx_proto = clang::cast<clang::FunctionProtoType>(cxx_func->getType());
  std::vector<clang::QualType> param_types(cxx_proto->param_type_begin(),
                                           cxx_proto->param_type_end());
  // Create function prototype
  auto ret_type = GetAsCType(cxx_proto->getReturnType());
  auto func_type = ast_ctx.getFunctionType(ret_type, param_types,
                                           cxx_proto->getExtProtoInfo());
  // Declare the C function
  auto func_decl =
      ast.CreateFunctionDecl(c_tu, func_type, GetMangledName(cxx_func));
  // Declare parameters
  std::vector<clang::ParmVarDecl *> param_decls;
  for (auto cxx_param : cxx_func->parameters()) {
    param_decls.push_back(ast.CreateParamDecl(func_decl,
                                              GetAsCType(cxx_param->getType()),
                                              cxx_param->getNameAsString()));
  }
  // Set C function parameters
  func_decl->setParams(param_decls);
  // Save to C translation unit
  if (!clang::isa<clang::CXXMethodDecl>(cxx_func)) {
    // c_tu->addDecl(func_decl);
  } else {
    c_decls[cxx_func] = func_decl;
  }
  // Done
  return true;
}

bool CXXToCDeclVisitor::VisitCXXMethodDecl(clang::CXXMethodDecl *method) {
  auto name = method->getNameAsString();
  DLOG(INFO) << "VisitCXXMethodDecl: " << name;
  // Get the result of `VisitFunctionDecl`
  auto func_iter = c_decls.find(method);
  CHECK(func_iter != c_decls.end())
      << "Method " << name
      << " does not have a C function equivalent created by VisitFunctionDecl";
  // Get C struct equivalent of `method` parent class
  auto struct_iter = c_decls.find(method->getParent());
  CHECK(struct_iter != c_decls.end())
      << "Method " << name << " does not have a parent";
  // Get the `this` pointer type
  auto struct_decl = clang::cast<clang::RecordDecl>(struct_iter->second);
  auto struct_type = struct_decl->getTypeForDecl();
  auto this_type = ast_ctx.getPointerType(clang::QualType(struct_type, 0));
  // Transfer stuff from the declaration made by `VisitFunctionDecl`
  auto old_func = clang::cast<clang::FunctionDecl>(func_iter->second);
  auto old_proto = clang::cast<clang::FunctionProtoType>(old_func->getType());
  std::vector<clang::QualType> param_types({this_type});
  param_types.insert(param_types.end(), old_proto->param_type_begin(),
                     old_proto->param_type_end());
  // Create function prototype
  auto func_type = ast_ctx.getFunctionType(
      old_proto->getReturnType(), param_types, old_proto->getExtProtoInfo());
  // Declare the C function
  auto func_decl =
      ast.CreateFunctionDecl(c_tu, func_type, old_func->getIdentifier());
  // Declare parameters
  auto this_decl = ast.CreateParamDecl(func_decl, this_type, "this");
  std::vector<clang::ParmVarDecl *> param_decls({this_decl});
  param_decls.insert(param_decls.end(), old_func->param_begin(),
                     old_func->param_end());
  // Set C function parameters
  func_decl->setParams(param_decls);
  // Save to C translation unit
  c_tu->addDecl(func_decl);
  // Done
  return true;
}

bool CXXToCDeclVisitor::VisitRecordDecl(clang::RecordDecl *record) {
  // auto name = record->getNameAsString();
  // DLOG(INFO) << "VisitRecordDecl: " << name;
  return true;
}

bool CXXToCDeclVisitor::VisitCXXRecordDecl(clang::CXXRecordDecl *cls) {
  auto name = cls->getNameAsString();
  DLOG(INFO) << "VisitCXXRecordDecl: " << name;
  if (c_decls.count(cls)) {
    LOG(WARNING) << "Asking to re-generate class " << name << " ; returning";
    return true;
  }
  // Create a vtable
  if (cls->isPolymorphic()) {
  }
  // Create the C struct definition
  auto decl = ast.CreateStructDecl(c_tu, GetMangledName(cls));
  // Complete the C struct definition
  decl->completeDefinition();
  // Save the result
  c_decls[cls] = decl;
  // Add the C struct to the C translation unit
  c_tu->addDecl(decl);
  // Done
  return true;
}

bool CXXToCDeclVisitor::VisitFieldDecl(clang::FieldDecl *field) {
  auto name = field->getNameAsString();
  DLOG(INFO) << "FieldDecl: " << name;
  // Get parent C struct
  auto iter = c_decls.find(field->getParent());
  CHECK(iter != c_decls.end()) << "Field " << name << " does not have a parent";
  auto parent = clang::cast<clang::RecordDecl>(iter->second);
  // Create the field
  auto type = GetAsCType(field->getType());
  auto decl = ast.CreateFieldDecl(parent, type, field->getName().str());
  // Save the result
  c_decls[field] = decl;
  // Add the field to it's parent struct
  parent->addDecl(decl);
  // Done
  return true;
}

}  // namespace rellic