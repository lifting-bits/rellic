/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/Z3CondSimplify.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/Util.h"

namespace rellic {

Z3CondSimplify::Z3CondSimplify(DecompilationContext& dec_ctx,
                               clang::ASTUnit& unit)
    : ASTPass(dec_ctx, unit) {}

void Z3CondSimplify::RunImpl() {
  LOG(INFO) << "Simplifying conditions using Z3";
  for (size_t i{0}; i < dec_ctx.z3_exprs.size() && !Stopped(); ++i) {
    auto simpl{OrderById(dec_ctx.z3_exprs[i].simplify())};
    dec_ctx.z3_exprs.set(i, simpl);
  }
}

}  // namespace rellic
