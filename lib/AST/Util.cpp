/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/Util.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

namespace rellic {

unsigned GetHash(clang::ASTContext &ctx, clang::Stmt *stmt) {
  llvm::FoldingSetNodeID id;
  stmt->Profile(id, ctx, /*Canonical=*/true);
  return id.ComputeHash();
}

static bool IsEquivalent(clang::ASTContext &ctx, clang::Stmt *a, clang::Stmt *b,
                         llvm::FoldingSetNodeID &foldingSetA,
                         llvm::FoldingSetNodeID &foldingSetB) {
  a->Profile(foldingSetA, ctx, /*Canonical=*/true);
  b->Profile(foldingSetB, ctx, /*Canonical=*/true);

  if (foldingSetA != foldingSetB) {
    return false;
  }

  CHECK_EQ(a->getStmtClass(), b->getStmtClass())
      << "Statement classes differ but folding sets don't";

  auto child_a{a->child_begin()};
  auto child_b{b->child_begin()};
  while (true) {
    bool a_end{child_a == a->child_end()};
    bool b_end{child_b == b->child_end()};
    if (a_end ^ b_end) {
      return false;
    } else if (a_end && b_end) {
      return true;
    } else if (!IsEquivalent(ctx, *child_a, *child_b, foldingSetA,
                             foldingSetB)) {
      return false;
    }

    ++child_a;
    ++child_b;
  }
}

bool IsEquivalent(clang::ASTContext &ctx, clang::Stmt *a, clang::Stmt *b) {
  llvm::FoldingSetNodeID idA, idB;
  return IsEquivalent(ctx, a, b, idA, idB);
}

bool Replace(clang::ASTContext &ctx, clang::Stmt *from, clang::Stmt *to,
             clang::Stmt **in) {
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

bool Replace(clang::ASTContext &ctx, clang::Expr *from, clang::Expr *to,
             clang::Expr **in) {
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

}  // namespace rellic
