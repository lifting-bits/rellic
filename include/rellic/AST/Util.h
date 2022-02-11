/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/DeclBase.h>

#include <unordered_map>

namespace rellic {

unsigned GetHash(clang::ASTContext &ctx, clang::Stmt *stmt);
bool IsEquivalent(clang::ASTContext &ctx, clang::Stmt *a, clang::Stmt *b);
bool Replace(clang::ASTContext &ctx, clang::Stmt *from, clang::Stmt *to,
             clang::Stmt **in);
bool Replace(clang::ASTContext &ctx, clang::Expr *from, clang::Expr *to,
             clang::Expr **in);

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

}  // namespace rellic