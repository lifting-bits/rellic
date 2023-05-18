/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include "rellic/AST/DecompilationContext.h"

namespace rellic {

unsigned GetHash(clang::ASTContext &ctx, clang::Stmt *stmt);
bool IsEquivalent(clang::Expr *a, clang::Expr *b);
template <typename TFrom, typename TIn>
bool Replace(TFrom *from, clang::Expr *to, TIn **in) {
  auto from_expr{clang::cast<clang::Expr>(from)};
  auto in_expr{clang::cast<clang::Expr>(*in)};
  if (IsEquivalent(in_expr, from_expr)) {
    *in = to;
    return true;
  } else {
    bool changed{false};
    for (auto child{(*in)->child_begin()}; child != (*in)->child_end();
         ++child) {
      changed |= Replace(from_expr, to, &*child);
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

template <typename TKey1, typename TKey2, typename TKey3, typename TValue>
void CopyProvenance(TKey1 *from, TKey2 *to,
                    std::unordered_map<TKey3 *, TValue *> &map) {
  map[to] = map[from];
}

clang::Expr *Clone(clang::ASTUnit &unit, clang::Expr *stmt,
                   DecompilationContext::ExprToUseMap &provenance);

std::string ClangThingToString(const clang::Stmt *stmt);
std::string ClangThingToString(clang::QualType ty);

z3::goal ApplyTactic(const z3::tactic &tactic, z3::expr expr);

bool Prove(z3::expr expr);

z3::expr HeavySimplify(z3::expr expr);
z3::expr_vector Clone(z3::expr_vector &vec);

// Tries to keep each subformula sorted by its id so that they don't get
// shuffled around by simplification
z3::expr OrderById(z3::expr expr);
}  // namespace rellic