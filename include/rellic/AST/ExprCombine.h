/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
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
 * This pass performs a number of different trasnformations on expressions,
 * like turning *&a into a, or !(a == b) into a != b
 */
class ExprCombine : public clang::StmtVisitor<ExprCombine>, public ASTPass {
 protected:
  void RunImpl(clang::Stmt *stmt) override;

 public:
  ExprCombine(StmtToIRMap &provenance, clang::ASTUnit &unit,
              Substitutions &substitutions);

  void VisitCStyleCastExpr(clang::CStyleCastExpr *cast);
  void VisitUnaryOperator(clang::UnaryOperator *op);
  void VisitBinaryOperator(clang::BinaryOperator *op);
  void VisitArraySubscriptExpr(clang::ArraySubscriptExpr *expr);
  void VisitMemberExpr(clang::MemberExpr *expr);
  void VisitParenExpr(clang::ParenExpr *paren);
};

}  // namespace rellic
