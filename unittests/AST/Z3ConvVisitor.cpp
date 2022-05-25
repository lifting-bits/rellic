/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/Z3ConvVisitor.h"

#include <z3++.h>

#include "Util.h"

TEST_SUITE("Z3ConvVisitor::VisitFunctionDecl") {
  SCENARIO("Create a z3::func_decl for a given clang::FunctionDecl") {
    GIVEN("Function `int f(int a, char b);`") {
      auto c_unit{GetASTUnit("int f(int a, char b);")};
      z3::context z_ctx;
      rellic::Z3ConvVisitor conv(*c_unit, &z_ctx);
      auto c_tudecl{c_unit->getASTContext().getTranslationUnitDecl()};
      auto c_fdecl{GetDecl<clang::FunctionDecl>(c_tudecl, "f")};
      THEN("return an uninterpreted function `f(bv32 bv8 bv32)`") {
        auto z_fdecl{conv.GetOrCreateZ3Decl(c_fdecl)};
        CHECK(z_fdecl.decl_kind() == Z3_OP_UNINTERPRETED);
        CHECK(z_fdecl.name().str() == c_fdecl->getNameAsString());
        CHECK(z_fdecl.arity() == c_fdecl->getNumParams());
        CHECK(z_fdecl.range().bv_size() == 32U);
        CHECK(z_fdecl.domain(0U).bv_size() == 32U);
        CHECK(z_fdecl.domain(1U).bv_size() == 8U);
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
      THEN("return an uninterpreted function application `(Call id f1 0 1)`") {
        auto z_call{conv.GetOrCreateZ3Expr(c_call)};
        CHECK(z_call.decl().name().str() == "Call");
        llvm::FoldingSetNodeID id;
        c_call->Profile(id, c_unit->getASTContext(), /*Canonical=*/true);
        CHECK(z_call.arg(0U).simplify().get_numeral_uint64() ==
              id.ComputeHash());
        auto z_callee{conv.GetOrCreateZ3Expr(c_call->getCallee())};
        CHECK(z3::eq(z_call.arg(1U), z_callee));
        CHECK(z_call.arg(2U).simplify().get_numeral_uint64() == 0U);
        CHECK(z_call.arg(3U).simplify().get_numeral_uint64() == 1U);
      }
    }
  }
}

// TEST_SUITE("Z3ConvVisitor::HandleZ3Call") {
//   SCENARIO("Create a clang::CallExpr for a `(Call 1 f 2 3)` z3::expr") {
//     GIVEN("Uninterpreted z3 function `f(bv32 bv32 bv32)`") {
//       auto c_unit{GetASTUnit("")};
//       z3::context z_ctx;
//       rellic::Z3ConvVisitor conv(*c_unit, &z_ctx);
//       auto z_sort{z_ctx.bv_sort(32U)};
//       auto z_func_decl{z_ctx.function("f", z_sort, z_sort, z_sort)};
//       auto z_func{z3::as_array(z_func_decl)};
//       auto z_call{z_ctx.function("Call", z_sort, z_func.get_sort(), z_sort,
//                                  z_sort, z_func.get_sort().array_range())};
//       GIVEN("Application `(Call 1 f 2 3)`") {
//         auto z_bv_1{z_ctx.bv_val(1U, 32U)};
//         auto z_bv_2{z_ctx.bv_val(2U, 32U)};
//         auto z_bv_3{z_ctx.bv_val(3U, 32U)};
//         auto z_app{z_call(z_bv_1, z_func, z_bv_2, z_bv_3)};
//         THEN("return a `f(2U, 3U)` clang::CallExpr") {
//           auto c_call{conv.GetOrCreateCExpr(z_app)};
//           CHECK(c_call != nullptr);
//         }
//       }
//     }
//   }
// }
