/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <unordered_map>

#include "rellic/AST/DebugInfoCollector.h"
#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/TransformVisitor.h"

namespace rellic {

using TypeDeclToIRMap = std::unordered_map<clang::TypeDecl *, llvm::Type *>;

class StructFieldRenamer
    : public ASTPass,
      public clang::RecursiveASTVisitor<StructFieldRenamer> {
 private:
  TypeDeclToIRMap decls;
  IRTypeToDITypeMap &types;
  IRToTypeDeclMap &inv_decl;

 protected:
  void RunImpl() override;

 public:
  StructFieldRenamer(StmtToIRMap &provenance, ExprToUseMap &use_provenance,
                     clang::ASTUnit &unit, IRTypeToDITypeMap &types,
                     IRToTypeDeclMap &decls);

  bool VisitRecordDecl(clang::RecordDecl *decl);
};

}  // namespace rellic