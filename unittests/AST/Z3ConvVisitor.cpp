/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/Z3ConvVisitor.h"

#include "Util.h"

TEST_SUITE("Z3ConvVisitor::VisitFunctionDecl") {
  SCENARIO("Create a z3::func_decl for a given clang::FunctionDecl") {
    GIVEN("Function declaration `void f(int a, int b);`") {
      auto c_unit{GetASTUnit("void f(int a, int b);")};
      z3::context z_ctx;
      rellic::Z3ConvVisitor conv(*c_unit, &z_ctx);
      auto c_tudecl{c_unit->getASTContext().getTranslationUnitDecl()};
      auto c_fdecl{GetDecl<clang::FunctionDecl>(c_tudecl, "f")};
      THEN("return an uninterpreted function `f(int, bv, bv)`") {
        auto z_fdecl{conv.GetOrCreateZ3Decl(c_fdecl)};
        CHECK(z_fdecl.name().str() == c_fdecl->getNameAsString());
      }
    }
  }
}