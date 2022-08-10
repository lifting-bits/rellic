/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/Z3CondSimplify.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Expr.h>
#include <clang/AST/OperationKinds.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

namespace rellic {

Z3CondSimplify::Z3CondSimplify(Provenance &provenance, clang::ASTUnit &unit)
    : ASTPass(provenance, unit) {}

void Z3CondSimplify::RunImpl() {
  LOG(INFO) << "Simplifying conditions using Z3";
  for (size_t i{0}; i < provenance.z3_exprs.size() && !Stopped(); ++i) {
    auto simpl{Sort(provenance.z3_exprs[i].simplify())};
    provenance.z3_exprs.set(i, simpl);
  }
}

}  // namespace rellic
