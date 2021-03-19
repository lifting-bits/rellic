/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/ASTBuilder.h"

#include <clang/Tooling/Tooling.h>
#include <doctest/doctest.h>

TEST_SUITE("ASTBuilder::CreateIntLit") {
  SCENARIO("Create clang::IntegerLiteral for 1 bit integer value") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{clang::tooling::buildASTFromCode("", "out.c")};

      REQUIRE(unit != nullptr);

      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(ctx);

      GIVEN("1 bit unsigned llvm::APInt") {
        llvm::APInt api(1U, 0U, /*isSigned=*/false);
        auto lit{ast.CreateIntLit(api)};
        THEN("return a `unsigned int` typed integer literal") {
          REQUIRE(lit != nullptr);
          CHECK(clang::isa<clang::IntegerLiteral>(lit));
          CHECK(lit->getType() == ctx.UnsignedIntTy);
        }
      }
    }
  }

  SCENARIO("Create clang::IntegerLiteral for 8 bit integer value") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{clang::tooling::buildASTFromCode("", "out.c")};

      REQUIRE(unit != nullptr);

      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(ctx);

      GIVEN("8 bit unsigned llvm::APInt") {
        llvm::APInt api(8U, 42U, /*isSigned=*/false);
        auto lit{ast.CreateIntLit(api)};
        THEN("return a `unsigned char` typed integer literal") {
          REQUIRE(lit != nullptr);
          CHECK(clang::isa<clang::CStyleCastExpr>(lit));
          CHECK(clang::isa<clang::IntegerLiteral>(lit->IgnoreCasts()));
          CHECK(lit->getType() == ctx.UnsignedCharTy);
        }
      }
    }
  }

  SCENARIO("Create clang::IntegerLiteral for 16 bit integer value") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{clang::tooling::buildASTFromCode("", "out.c")};

      REQUIRE(unit != nullptr);

      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(ctx);

      GIVEN("16 bits wide llvm::APInt") {
        llvm::APInt api(16U, 42U, /*isSigned=*/false);
        auto lit{ast.CreateIntLit(api)};
        THEN("return a `unsigned short` typed integer literal") {
          REQUIRE(lit != nullptr);
          CHECK(clang::isa<clang::IntegerLiteral>(lit));
          CHECK(lit->getType() == ctx.UnsignedShortTy);
        }
      }
    }
  }

  SCENARIO("Create clang::IntegerLiteral for 32 bit integer value") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{clang::tooling::buildASTFromCode("", "out.c")};

      REQUIRE(unit != nullptr);

      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(ctx);

      GIVEN("32 bits wide llvm::APInt") {
        llvm::APInt api(32U, 42U, /*isSigned=*/false);
        auto lit{ast.CreateIntLit(api)};
        THEN("return a `unsigned int` typed integer literal") {
          REQUIRE(lit != nullptr);
          CHECK(clang::isa<clang::IntegerLiteral>(lit));
          CHECK(lit->getType() == ctx.UnsignedIntTy);
        }
      }
    }
  }

  SCENARIO("Create clang::IntegerLiteral for 64 bit integer value") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{clang::tooling::buildASTFromCode("", "out.c")};

      REQUIRE(unit != nullptr);

      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(ctx);

      GIVEN("64 bits wide llvm::APInt") {
        llvm::APInt api(64U, 42U, /*isSigned=*/false);
        auto lit{ast.CreateIntLit(api)};
        THEN("return a `unsigned long` typed integer literal") {
          REQUIRE(lit != nullptr);
          CHECK(clang::isa<clang::IntegerLiteral>(lit));
          CHECK(lit->getType() == ctx.UnsignedLongTy);
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateCharLit") {
  SCENARIO("Create clang::CharacterLiteral for 8 bit integer value") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{clang::tooling::buildASTFromCode("", "out.c")};

      REQUIRE(unit != nullptr);

      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(ctx);

      GIVEN("8 bit unsigned llvm::APInt") {
        llvm::APInt api(8U, 'x', /*isSigned=*/false);
        auto lit{ast.CreateCharLit(api)};
        THEN("return a `int` typed character literal") {
          REQUIRE(lit != nullptr);
          CHECK(clang::isa<clang::CharacterLiteral>(lit));
          CHECK(lit->getType() == ctx.IntTy);
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateStrLit") {
  SCENARIO("Create clang::StringLiteral from std::string") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{clang::tooling::buildASTFromCode("", "out.c")};

      REQUIRE(unit != nullptr);

      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(ctx);

      GIVEN("std::string object by value") {
        // auto lit{ast.CreateStrLit("a string")};
        // THEN("return a `int` typed character literal") {
        //   REQUIRE(lit != nullptr);
        //   CHECK(clang::isa<clang::CharacterLiteral>(lit));
        //   CHECK(lit->getType() == ctx.IntTy);
        // }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateFPLit") {
  SCENARIO("Create clang::FloatingLiteral for 32 bit IEEE 754 value") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{clang::tooling::buildASTFromCode("", "out.c")};

      REQUIRE(unit != nullptr);

      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(ctx);

      GIVEN("`float` initialized llvm::APFloat") {
        auto lit{ast.CreateFPLit(llvm::APFloat(float(3.14)))};
        THEN("return a `float` typed floating point literal") {
          REQUIRE(lit != nullptr);
          CHECK(clang::isa<clang::FloatingLiteral>(lit));
          CHECK(lit->getType() == ctx.FloatTy);
        }
      }
    }
  }

  SCENARIO("Create clang::FloatingLiteral for 64 bit IEEE 754 value") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{clang::tooling::buildASTFromCode("", "out.c")};

      REQUIRE(unit != nullptr);

      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(ctx);

      GIVEN("`double` initialized llvm::APFloat") {
        auto lit{ast.CreateFPLit(llvm::APFloat(double(3.14)))};
        THEN("Return a `double` typed floating point literal") {
          REQUIRE(lit != nullptr);
          CHECK(clang::isa<clang::FloatingLiteral>(lit));
          CHECK(lit->getType() == ctx.DoubleTy);
        }
      }
    }
  }
}