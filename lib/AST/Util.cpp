/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/Util.h"

#include <clang/AST/Stmt.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

namespace rellic {

unsigned GetHash(clang::ASTContext &ctx, clang::Stmt *stmt) {
  llvm::FoldingSetNodeID id;
  stmt->Profile(id, ctx, /*Canonical=*/true);
  return id.ComputeHash();
}

}  // namespace rellic
