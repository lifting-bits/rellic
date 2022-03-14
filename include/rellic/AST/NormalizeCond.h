/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/StmtVisitor.h>

#include "rellic/AST/ASTPass.h"

namespace rellic {

/*
 * This pass turns conditions into conjunctive normal form (CNF). Warning: this
 * has the potential of creating an exponential number of terms, so it's best to
 * perform this pass after simplification.
 */
class NormalizeCond : public clang::StmtVisitor<NormalizeCond>, public ASTPass {
 protected:
  void RunImpl(clang::Stmt *stmt) override;

 public:
  static char ID;

  NormalizeCond(StmtToIRMap &provenance, clang::ASTUnit &unit,
                Substitutions &substitutions);

  void VisitUnaryOperator(clang::UnaryOperator *op);
  void VisitBinaryOperator(clang::BinaryOperator *op);
};

}  // namespace rellic
