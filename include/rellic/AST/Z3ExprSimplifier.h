/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once
#include "rellic/AST/ASTBuilder.h"
#include "rellic/AST/Z3ConvVisitor.h"

namespace rellic {
class Z3ExprSimplifier {
  clang::ASTContext* ast_ctx;
  ASTBuilder ast;

  std::unique_ptr<z3::context> z_ctx;
  std::unique_ptr<rellic::Z3ConvVisitor> z_gen;

  z3::tactic tactic;

  bool Prove(z3::expr e);
  z3::expr ToZ3(clang::Expr* e);

 public:
  Z3ExprSimplifier(clang::ASTUnit& unit);

  z3::context& GetZ3Context() { return *z_ctx; }

  void SetZ3Tactic(z3::tactic t) { tactic = t; };

  clang::Expr* Simplify(clang::Expr* e);
};
}  // namespace rellic