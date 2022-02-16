/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include "rellic/AST/TransformVisitor.h"

namespace rellic {

/*
 * This pass performs a number of different trasnformations on expressions,
 * like turning *&a into a, or !(a == b) into a != b
 */
class ExprCombine : public TransformVisitor<ExprCombine> {
 protected:
  void RunImpl() override;

 public:
  ExprCombine(StmtToIRMap &provenance, clang::ASTUnit &unit);

  bool VisitCStyleCastExpr(clang::CStyleCastExpr *cast);
  bool VisitUnaryOperator(clang::UnaryOperator *op);
  bool VisitBinaryOperator(clang::BinaryOperator *op);
  bool VisitArraySubscriptExpr(clang::ArraySubscriptExpr *expr);
  bool VisitMemberExpr(clang::MemberExpr *expr);
  bool VisitParenExpr(clang::ParenExpr *paren);
};

}  // namespace rellic
