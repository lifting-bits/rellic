/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/DeclBase.h>
#include <clang/AST/Stmt.h>
#include <clang/Frontend/ASTUnit.h>
#include <llvm/IR/Value.h>

#include <unordered_map>

namespace rellic {

unsigned GetHash(clang::ASTContext &ctx, clang::Stmt *stmt);
bool IsEquivalent(clang::ASTContext &ctx, clang::Stmt *a, clang::Stmt *b);
template <typename TFrom, typename TTo, typename TIn>
bool Replace(clang::ASTContext &ctx, TFrom *from, TTo *to, TIn **in) {
  if (IsEquivalent(ctx, *in, from)) {
    *in = to;
    return true;
  } else {
    bool changed{false};
    for (auto child{(*in)->child_begin()}; child != (*in)->child_end();
         ++child) {
      changed |= Replace(ctx, from, to, &*child);
    }
    return changed;
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

std::string ClangThingToString(clang::Stmt *stmt);

}  // namespace rellic