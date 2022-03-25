/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include "rellic/AST/TransformVisitor.h"

namespace rellic {

/*
 * This pass turns conditions into conjunctive normal form (CNF). Warning: this
 * has the potential of creating an exponential number of terms, so it's best to
 * perform this pass after simplification.
 */
class NormalizeCond : public TransformVisitor<NormalizeCond> {
 protected:
  void RunImpl() override;

 public:
  static char ID;

  NormalizeCond(StmtToIRMap &provenance, ExprToUseMap &use_provenance,
                clang::ASTUnit &unit);

  bool VisitUnaryOperator(clang::UnaryOperator *op);
  bool VisitBinaryOperator(clang::BinaryOperator *op);
};

}  // namespace rellic
