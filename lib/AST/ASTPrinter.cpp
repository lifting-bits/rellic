/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/ASTPrinter.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

namespace rellic {

std::string ASTPrinter::print(clang::Decl *decl) {
  std::string str;
  llvm::raw_string_ostream ss(str);
  decl->print(ss);
  return ss.str();
}

std::string ASTPrinter::print(clang::Stmt *stmt) {
  std::string str;
  llvm::raw_string_ostream ss(str);
  stmt->printPretty(ss, /*PrinterHelper=*/nullptr,
                    unit.getASTContext().getPrintingPolicy());
  return ss.str();
}

std::string ASTPrinter::print(clang::QualType type) {
  return type.getAsString(unit.getASTContext().getPrintingPolicy());
}

bool ASTPrinter::VisitTranslationUnitDecl(clang::TranslationUnitDecl *tudecl) {
  for (auto decl : tudecl->decls()) {
    if (!decl_strs.count(decl)) {
      continue;
    }
    os << decl_strs[decl];
    auto fdecl{decl->getAsFunction()};
    if (!fdecl || !fdecl->isThisDeclarationADefinition()) {
      os << ";";
    }
  }
  return true;
}

bool ASTPrinter::VisitFunctionDecl(clang::FunctionDecl *fdecl) {
  auto &decl_str{decl_strs[fdecl]};
  if (!decl_str.empty()) {
    return true;
  }
  std::stringstream ss;
  // Return type
  ss << print(fdecl->getReturnType());
  // Name
  ss << ' ' << fdecl->getName().str();
  // Parameters
  ss << '(';
  // Handle non-variadic functions with no params
  if (fdecl->getNumParams() == 0U && !fdecl->isVariadic()) {
    ss << "void";
  }
  // Handle params
  for (auto i{0U}; i != fdecl->getNumParams(); ++i) {
    if (i) {
      ss << ", ";
    }
    ss << print(fdecl->getParamDecl(i));
  }
  // Handle variadic functions
  if (fdecl->isVariadic()) {
    ss << ", ...";
  }
  ss << ')';
  // Body
  if (fdecl->isThisDeclarationADefinition()) {
    ss << print(fdecl->getBody());
  }

  decl_str = ss.str();

  return true;
}

bool ASTPrinter::VisitDecl(clang::Decl *decl) {
  auto &decl_str{decl_strs[decl]};
  if (!decl_str.empty()) {
    return true;
  }
  
  decl_str = print(decl);
  
  return true;
}

}  // namespace rellic