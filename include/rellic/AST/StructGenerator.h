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
#include <unordered_set>
#include <vector>

#include "rellic/AST/ASTBuilder.h"

namespace rellic {

class StructGenerator {
  clang::ASTContext& ast_ctx;
  rellic::ASTBuilder ast;
  std::unordered_map<llvm::DICompositeType*, clang::RecordDecl*>
      fwd_decl_records{};
  std::unordered_map<llvm::DICompositeType*, clang::EnumDecl*> fwd_decl_enums{};
  std::unordered_map<llvm::DIDerivedType*, clang::TypedefNameDecl*>
      typedef_decls{};
  unsigned decl_count{0};

  using DeclToDbgInfo =
      std::unordered_map<clang::FieldDecl*, llvm::DIDerivedType*>;
  void VisitFields(clang::RecordDecl* decl, llvm::DICompositeType* s,
                   DeclToDbgInfo& map, bool isUnion);

  std::string GetAnonName(llvm::DICompositeType* t);

  clang::QualType BuildArray(llvm::DICompositeType* a);
  clang::QualType BuildBasic(llvm::DIBasicType* b, int sizeHint);
  clang::QualType BuildSubroutine(llvm::DISubroutineType* s);
  clang::QualType BuildComposite(llvm::DICompositeType* type);
  clang::QualType BuildDerived(llvm::DIDerivedType* d, int sizeHint);
  clang::QualType BuildType(llvm::DIType* t, int sizeHint = -1);

  void DefineComposite(llvm::DICompositeType* s);
  void DefineStruct(llvm::DICompositeType* s);
  void DefineUnion(llvm::DICompositeType* s);
  void DefineEnum(llvm::DICompositeType* s);

  void VisitType(llvm::DIType* t, std::vector<llvm::DICompositeType*>& list,
                 std::unordered_set<llvm::DIType*>& visited, bool fwdDecl);

 public:
  StructGenerator(clang::ASTUnit& ast_unit);

  clang::QualType GetType(llvm::DIType* t);

  template <typename It>
  void GenerateDecls(It begin, It end) {
    std::vector<llvm::DICompositeType*> sorted_types{};
    std::unordered_set<llvm::DIType*> visited_types{};
    for (auto i{begin}; i != end; ++i) {
      VisitType(*i, sorted_types, visited_types, /*fwdDecl=*/false);
    }

    for (auto type : sorted_types) {
      DefineComposite(type);
    }
  }

  std::vector<clang::Expr*> GetAccessor(clang::Expr* base,
                                        clang::RecordDecl* decl,
                                        unsigned offset, unsigned length);
};
}  // namespace rellic