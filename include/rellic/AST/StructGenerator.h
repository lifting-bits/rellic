/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/Decl.h>
#include <llvm/IR/DebugInfoMetadata.h>

#include <unordered_map>
#include <vector>

#include "rellic/AST/ASTBuilder.h"

namespace rellic {

class StructGenerator {
  clang::ASTContext& ast_ctx;
  rellic::ASTBuilder ast;
  std::unordered_map<llvm::DICompositeType*, clang::RecordDecl*> record_decls{};
  std::unordered_map<llvm::DICompositeType*, clang::EnumDecl*> enum_decls{};
  std::unordered_map<llvm::DIDerivedType*, clang::TypedefNameDecl*>
      typedef_decls{};
  std::unordered_map<llvm::DIType*, clang::QualType> types{};
  unsigned decl_count{0};

  using DeclToDbgInfo =
      std::unordered_map<clang::FieldDecl*, llvm::DIDerivedType*>;
  void VisitFields(clang::RecordDecl* decl, llvm::DICompositeType* s,
                   DeclToDbgInfo& map, bool isUnion);

  clang::QualType VisitEnum(llvm::DICompositeType* e, bool fwdDecl);
  clang::QualType VisitStruct(llvm::DICompositeType* s, bool fwdDecl);
  clang::QualType VisitUnion(llvm::DICompositeType* u, bool fwdDecl);
  clang::QualType VisitArray(llvm::DICompositeType* a);
  clang::QualType VisitBasic(llvm::DIBasicType* b, int sizeHint);
  clang::QualType VisitSubroutine(llvm::DISubroutineType* s);
  clang::QualType VisitComposite(llvm::DICompositeType* type, bool fwdDecl);
  clang::QualType VisitDerived(llvm::DIDerivedType* d, bool fwdDecl,
                               int sizeHint);

 public:
  StructGenerator(clang::ASTUnit& ast_unit);

  clang::QualType VisitType(llvm::DIType* t, bool forwardDeclaration = false,
                            int sizeHint = -1);
  std::vector<clang::Expr*> GetAccessor(clang::Expr* base,
                                        clang::RecordDecl* decl,
                                        unsigned offset, unsigned length);
};
}  // namespace rellic