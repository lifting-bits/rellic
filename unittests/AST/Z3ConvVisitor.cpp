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
    GIVEN("Function `int f(int a, char b);`") {
      auto c_unit{GetASTUnit("int f(int a, char b);")};
      z3::context z_ctx;
      rellic::Z3ConvVisitor conv(*c_unit, &z_ctx);
      auto c_tudecl{c_unit->getASTContext().getTranslationUnitDecl()};
      auto c_fdecl{GetDecl<clang::FunctionDecl>(c_tudecl, "f")};
      THEN("return an uninterpreted function `bv32 f(bv32, bv8)`") {
        auto z_fdecl{conv.GetOrCreateZ3Decl(c_fdecl)};
        CHECK(z_fdecl.decl_kind() == Z3_OP_UNINTERPRETED);
        CHECK(z_fdecl.name().str() == c_fdecl->getNameAsString());
        CHECK(z_fdecl.arity() == c_fdecl->getNumParams() + 1);
        CHECK(z_fdecl.range().bv_size() == 32U);
        CHECK(z_fdecl.domain(0U).is_int());
        CHECK(z_fdecl.domain(1U).bv_size() == 32U);
        CHECK(z_fdecl.domain(2U).bv_size() == 8U);
      }
    }
  }
}

TEST_SUITE("Z3ConvVisitor::VisitCallExpr") {
  SCENARIO("Create a z3::expr for a given clang::CallExpr") {
    GIVEN("Functions `void f1(int a, char b); void f2(void){ f1(0, 1); }`") {
      auto c_unit{
          GetASTUnit("void f1(int a, char b);"
                     "void f2(void){ f1(0, 1); }")};
      z3::context z_ctx;
      rellic::Z3ConvVisitor conv(*c_unit, &z_ctx);
      auto c_tudecl{c_unit->getASTContext().getTranslationUnitDecl()};
      auto c_fdecl{GetDecl<clang::FunctionDecl>(c_tudecl, "f2")};
      auto c_func_body{clang::cast<clang::CompoundStmt>(c_fdecl->getBody())};
      auto c_call{clang::cast<clang::CallExpr>(*c_func_body->body_begin())};
      THEN("return an uninterpreted function application `f1(id 0 1)`") {
        auto z_call{conv.GetOrCreateZ3Expr(c_call)};
        CHECK(z_call.decl().name().str() == "f1");
        llvm::FoldingSetNodeID id;
        c_call->Profile(id, c_unit->getASTContext(), /*Canonical=*/true);
        CHECK(z_call.arg(0U).simplify().get_numeral_uint64() ==
              id.ComputeHash());
        CHECK(z_call.arg(1U).simplify().get_numeral_uint64() == 0U);
        CHECK(z_call.arg(2U).simplify().get_numeral_uint64() == 1U);
      }
    }
  }
}