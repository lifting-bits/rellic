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

using StmtMap = std::unordered_map<clang::Stmt *, clang::Stmt *>;

bool ReplaceChildren(clang::Stmt *stmt, StmtMap &repl_map);

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