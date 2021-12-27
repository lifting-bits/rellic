/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/StructGenerator.h"

#include <clang/AST/Decl.h>

#include "Util.h"
#include "rellic/AST/ASTBuilder.h"

template <typename T>
static clang::DeclRefExpr *GetDeclRef(rellic::ASTBuilder &ast,
                                      clang::DeclContext *decl_ctx,
                                      const std::string &name) {
  auto ref{ast.CreateDeclRef(GetDecl<T>(decl_ctx, name))};
  REQUIRE(ref != nullptr);
  return ref;
}

TEST_SUITE("StructGenerator::GetAccessor") {
  SCENARIO("Create accessors for non packed struct") {
    GIVEN("Struct definition s") {
      std::vector<std::string> args{"-target", "x86_64-pc-linux-gnu"};
      auto unit{GetASTUnit("struct s { char c; int i; } x;", args)};
      auto &ctx{unit->getASTContext()};
      auto tudecl{ctx.getTranslationUnitDecl()};
      rellic::ASTBuilder ast(*unit);
      rellic::StructGenerator gen(*unit);
      auto var{GetDeclRef<clang::VarDecl>(ast, tudecl, "x")};
      auto strct{GetDecl<clang::RecordDecl>(tudecl, "s")};
      THEN("return correct accessors") {
        auto accessors_i{gen.GetAccessor(var, strct, 32, 32)};
        CHECK_EQ(accessors_i.size(), 1);

        auto accessors_c{gen.GetAccessor(var, strct, 0, 8)};
        CHECK_EQ(accessors_c.size(), 1);

        auto accessors_invalid{gen.GetAccessor(var, strct, 8, 32)};
        CHECK_EQ(accessors_invalid.size(), 0);
      }
    }
  }

  SCENARIO("Create accessors for non packed bitfield struct") {
    GIVEN("Struct definition s") {
      std::vector<std::string> args{"-target", "x86_64-pc-linux-gnu"};
      auto unit{GetASTUnit("struct s { int i : 3; int j : 3; } x;", args)};
      auto &ctx{unit->getASTContext()};
      auto tudecl{ctx.getTranslationUnitDecl()};
      rellic::ASTBuilder ast(*unit);
      rellic::StructGenerator gen(*unit);
      auto var{GetDeclRef<clang::VarDecl>(ast, tudecl, "x")};
      auto strct{GetDecl<clang::RecordDecl>(tudecl, "s")};
      THEN("return correct accessors") {
        auto accessors_i{gen.GetAccessor(var, strct, 0, 3)};
        CHECK_EQ(accessors_i.size(), 1);

        auto accessors_j{gen.GetAccessor(var, strct, 3, 3)};
        CHECK_EQ(accessors_j.size(), 1);

        auto accessors_invalid{gen.GetAccessor(var, strct, 0, 8)};
        CHECK_EQ(accessors_invalid.size(), 0);
      }
    }
  }

  SCENARIO("Create accessors for union") {
    GIVEN("Union definition u") {
      std::vector<std::string> args{"-target", "x86_64-pc-linux-gnu"};
      auto unit{GetASTUnit("union u { int i; int j; char c; } x;", args)};
      auto &ctx{unit->getASTContext()};
      auto tudecl{ctx.getTranslationUnitDecl()};
      rellic::ASTBuilder ast(*unit);
      rellic::StructGenerator gen(*unit);
      auto var{GetDeclRef<clang::VarDecl>(ast, tudecl, "x")};
      auto strct{GetDecl<clang::RecordDecl>(tudecl, "u")};
      THEN("return correct accessors") {
        auto accessors_ij{gen.GetAccessor(var, strct, 0, 32)};
        CHECK_EQ(accessors_ij.size(), 2);

        auto accessors_c{gen.GetAccessor(var, strct, 0, 8)};
        CHECK_EQ(accessors_c.size(), 1);

        auto accessors_invalid{gen.GetAccessor(var, strct, 32, 8)};
        CHECK_EQ(accessors_invalid.size(), 0);
      }
    }
  }

  SCENARIO("Create accessors for nested struct") {
    GIVEN("Struct definition s") {
      std::vector<std::string> args{"-target", "x86_64-pc-linux-gnu"};
      auto unit{GetASTUnit(
          "struct s { int i; union { struct { int j; char c; }; int k; }; } x;",
          args)};
      auto &ctx{unit->getASTContext()};
      auto tudecl{ctx.getTranslationUnitDecl()};
      rellic::ASTBuilder ast(*unit);
      rellic::StructGenerator gen(*unit);
      auto var{GetDeclRef<clang::VarDecl>(ast, tudecl, "x")};
      auto strct{GetDecl<clang::RecordDecl>(tudecl, "s")};
      THEN("return correct accessors") {
        auto accessors_i{gen.GetAccessor(var, strct, 0, 32)};
        CHECK_EQ(accessors_i.size(), 1);

        auto accessors_jk{gen.GetAccessor(var, strct, 32, 32)};
        CHECK_EQ(accessors_jk.size(), 2);

        auto accessors_c{gen.GetAccessor(var, strct, 64, 8)};
        CHECK_EQ(accessors_c.size(), 1);

        auto accessors_invalid{gen.GetAccessor(var, strct, 32, 8)};
        CHECK_EQ(accessors_invalid.size(), 0);
      }
    }
  }
}