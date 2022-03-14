/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/DeclBase.h>
#include <clang/Frontend/ASTUnit.h>
#include <llvm/IR/Value.h>

#include <unordered_map>

namespace rellic {

struct Substitution;

unsigned GetHash(clang::Stmt *stmt);
bool IsEquivalent(clang::Stmt *a, clang::Stmt *b);
template <typename TFrom, typename TTo, typename TIn>
void Replace(TFrom *from, TTo *to, TIn *in,
             std::vector<Substitution> &substitutions) {
  if (IsEquivalent(in, from)) {
    substitutions.push_back({in, to, "Replace"});
  } else {
    for (auto child{in->child_begin()}; child != in->child_end(); ++child) {
      Replace(from, to, *child, substitutions);
    }
  }
}

template <typename T>
size_t GetNumDecls(clang::DeclContext *decl_ctx) {
  size_t result = 0;
  for (auto decl : decl_ctx->decls()) {
    if (clang::isa<T>(decl)) {
      ++result;
    }
  }
  return result;
}

using StmtToIRMap = std::unordered_multimap<clang::Stmt *, llvm::Value *>;
void CopyProvenance(clang::Stmt *from, clang::Stmt *to, StmtToIRMap &map);

clang::Expr *Clone(clang::ASTUnit &unit, clang::Expr *stmt,
                   StmtToIRMap &provenance);
clang::Stmt *Clone(clang::ASTUnit &unit, clang::Stmt *stmt,
                   StmtToIRMap &provenance);

clang::Expr *ShallowClone(clang::ASTUnit &unit, clang::Expr *stmt,
                          StmtToIRMap &provenance);
clang::Stmt *ShallowClone(clang::ASTUnit &unit, clang::Stmt *stmt,
                          StmtToIRMap &provenance);

template <typename T>
T FindChild(clang::Stmt *stmt, clang::Stmt *child) {
  return std::find(stmt->child_begin(), stmt->child_end(), child);
}

}  // namespace rellic