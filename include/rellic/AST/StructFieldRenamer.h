/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/ASTUnit.h>

#include <unordered_map>

#include "rellic/AST/DebugInfoCollector.h"
#include "rellic/AST/IRToASTVisitor.h"

namespace rellic {

using TypeDeclToIRMap = std::unordered_map<clang::TypeDecl *, llvm::Type *>;

class StructFieldRenamer
    : public clang::RecursiveASTVisitor<StructFieldRenamer> {
 private:
  clang::ASTUnit &unit;
  clang::ASTContext &ast_ctx;
  ASTBuilder ast;
  TypeDeclToIRMap decls;
  IRTypeToDITypeMap &types;
  IRToTypeDeclMap &inv_decl;

 public:
  StructFieldRenamer(clang::ASTUnit &unit, IRTypeToDITypeMap &types,
                     IRToTypeDeclMap &decls);

  bool VisitRecordDecl(clang::RecordDecl *decl);
};

}  // namespace rellic