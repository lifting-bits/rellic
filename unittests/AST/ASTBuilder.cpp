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

namespace {
static std::unique_ptr<clang::ASTUnit> GetASTUnit(const char *code = "") {
  auto unit{clang::tooling::buildASTFromCode(code, "out.c")};
  REQUIRE(unit != nullptr);
  return unit;
}

static void IsNullPtrExprCheck(clang::ASTContext &ctx, clang::Expr *expr) {
  CHECK(
      expr->isNullPointerConstant(ctx, clang::Expr::NPC_NeverValueDependent) ==
      clang::Expr::NPCK_ZeroLiteral);
  CHECK(
      expr->isNullPointerConstant(ctx, clang::Expr::NPC_ValueDependentIsNull) ==
      clang::Expr::NPCK_ZeroLiteral);
  CHECK(expr->isNullPointerConstant(ctx,
                                    clang::Expr::NPC_ValueDependentIsNotNull) ==
        clang::Expr::NPCK_ZeroLiteral);
}

template <typename T>
static T *GetDecl(clang::DeclContext *decl_ctx, const std::string &name) {
  auto &ctx{decl_ctx->getParentASTContext()};
  auto lookup_result{decl_ctx->noload_lookup(&ctx.Idents.get(name))};
  REQUIRE(lookup_result.end() - lookup_result.begin() == 1);
  auto decl{clang::dyn_cast<T>(lookup_result.front())};
  REQUIRE(decl != nullptr);
  return decl;
}

}  // namespace

// TODO(surovic): Add test cases for signed llvm::APInt and group
// via SCENARIO rather than TEST_SUITE. This needs better
// support for value-parametrized tests from doctest. Should be in
// version 2.5.0
TEST_SUITE("ASTBuilder::CreateIntLit") {
  SCENARIO("Create clang::IntegerLiteral for 1 bit integer value") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("1 bit unsigned llvm::APInt") {
        llvm::APInt api(1U, 0U, /*isSigned=*/false);
        auto lit{ast.CreateIntLit(api)};
        THEN("return a unsigned int typed integer literal") {
          REQUIRE(lit != nullptr);
          CHECK(clang::isa<clang::IntegerLiteral>(lit));
          CHECK(lit->getType() == ctx.UnsignedIntTy);
        }
      }
    }
  }

  SCENARIO("Create clang::IntegerLiteral for 8 bit integer value") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("8 bit unsigned llvm::APInt") {
        llvm::APInt api(8U, 42U, /*isSigned=*/false);
        THEN("return a unsigned int typed integer literal") {
          auto lit{ast.CreateIntLit(api)};
          REQUIRE(lit != nullptr);
          CHECK(clang::isa<clang::IntegerLiteral>(lit));
          CHECK(lit->getType() == ctx.UnsignedIntTy);
        }

        THEN(
            "return a unsigned int typed integer literal casted to unsigned "
            "char") {
          auto cast{ast.CreateAdjustedIntLit(api)};
          REQUIRE(cast != nullptr);
          CHECK(clang::isa<clang::CStyleCastExpr>(cast));
          CHECK(cast->getType() == ctx.UnsignedCharTy);
          auto lit{cast->IgnoreCasts()};
          CHECK(clang::isa<clang::IntegerLiteral>(lit));
          CHECK(lit->getType() == ctx.UnsignedIntTy);
        }
      }
    }
  }

  SCENARIO("Create clang::IntegerLiteral for 16 bit integer value") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("16 bits wide llvm::APInt") {
        llvm::APInt api(16U, 42U, /*isSigned=*/false);
        THEN("return a unsigned int typed integer literal") {
          auto lit{ast.CreateIntLit(api)};
          REQUIRE(lit != nullptr);
          CHECK(clang::isa<clang::IntegerLiteral>(lit));
          CHECK(lit->getType() == ctx.UnsignedIntTy);
        }

        THEN(
            "return a unsigned int typed integer literal casted to unsigned "
            "short") {
          auto cast{ast.CreateAdjustedIntLit(api)};
          REQUIRE(cast != nullptr);
          CHECK(clang::isa<clang::CStyleCastExpr>(cast));
          CHECK(cast->getType() == ctx.UnsignedShortTy);
          auto lit{cast->IgnoreCasts()};
          CHECK(clang::isa<clang::IntegerLiteral>(lit));
          CHECK(lit->getType() == ctx.UnsignedIntTy);
        }
      }
    }
  }

  SCENARIO("Create clang::IntegerLiteral for 32 bit integer value") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("32 bits wide llvm::APInt") {
        llvm::APInt api(32U, 42U, /*isSigned=*/false);
        auto lit{ast.CreateIntLit(api)};
        THEN("return a unsigned int typed integer literal") {
          REQUIRE(lit != nullptr);
          CHECK(clang::isa<clang::IntegerLiteral>(lit));
          CHECK(lit->getType() == ctx.UnsignedIntTy);
        }
      }
    }
  }

  SCENARIO("Create clang::IntegerLiteral for 64 bit integer value") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("64 bits wide llvm::APInt") {
        llvm::APInt api(64U, 42U, /*isSigned=*/false);
        auto lit{ast.CreateIntLit(api)};
        THEN("return a unsigned long typed integer literal") {
          REQUIRE(lit != nullptr);
          CHECK(clang::isa<clang::IntegerLiteral>(lit));
          CHECK(lit->getType() == ctx.UnsignedLongTy);
        }
      }
    }
  }

  SCENARIO("Create clang::IntegerLiteral for 128 bit integer value") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("128 bits wide llvm::APInt") {
        llvm::APInt api(128U, 42U, /*isSigned=*/false);
        THEN("return a unsigned long long typed integer literal") {
          auto lit{ast.CreateIntLit(api)};
          REQUIRE(lit != nullptr);
          CHECK(clang::isa<clang::IntegerLiteral>(lit));
          CHECK(lit->getType() == ctx.UnsignedLongLongTy);
        }

        THEN(
            "return a unsigned long long typed integer literal casted to "
            "_uint128_t") {
          auto cast{ast.CreateAdjustedIntLit(api)};
          REQUIRE(cast != nullptr);
          CHECK(clang::isa<clang::CStyleCastExpr>(cast));
          CHECK(cast->getType() == ctx.UnsignedInt128Ty);
          auto lit{cast->IgnoreCasts()};
          CHECK(clang::isa<clang::IntegerLiteral>(lit));
          CHECK(lit->getType() == ctx.UnsignedLongLongTy);
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateCharLit") {
  SCENARIO("Create clang::CharacterLiteral for 8 bit integer value") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("8 bit wide unsigned llvm::APInt") {
        llvm::APInt api(8U, 'x', /*isSigned=*/false);
        auto lit{ast.CreateCharLit(api)};
        THEN("return a int typed character literal") {
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
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("std::string object by value") {
        std::string str("a string");
        auto lit{ast.CreateStrLit(str)};
        THEN("return a char[] typed character literal") {
          REQUIRE(lit != nullptr);
          CHECK(clang::isa<clang::StringLiteral>(lit));
          CHECK(lit->getType() ==
                ctx.getStringLiteralArrayType(ctx.CharTy, str.size()));
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateFPLit") {
  SCENARIO("Create clang::FloatingLiteral for 32 bit IEEE 754 value") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("float initialized llvm::APFloat") {
        auto lit{ast.CreateFPLit(llvm::APFloat(float(3.14)))};
        THEN("return a float typed floating point literal") {
          REQUIRE(lit != nullptr);
          CHECK(clang::isa<clang::FloatingLiteral>(lit));
          CHECK(lit->getType() == ctx.FloatTy);
        }
      }
    }
  }

  SCENARIO("Create clang::FloatingLiteral for 64 bit IEEE 754 value") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("double initialized llvm::APFloat") {
        auto lit{ast.CreateFPLit(llvm::APFloat(double(3.14)))};
        THEN("return a double typed floating point literal") {
          REQUIRE(lit != nullptr);
          CHECK(clang::isa<clang::FloatingLiteral>(lit));
          CHECK(lit->getType() == ctx.DoubleTy);
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateNull") {
  SCENARIO("Create clang::Expr representing a null pointer") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      THEN("return a 0U integer literal casted to a void pointer") {
        auto expr{ast.CreateNull()};
        REQUIRE(expr != nullptr);
        IsNullPtrExprCheck(ctx, expr);
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateUndef") {
  SCENARIO("Create clang::Expr whose value is undefined") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("an arbitrary type t") {
        auto type{ctx.DoubleTy};
        THEN("return a null pointer dereference of type t") {
          auto expr{ast.CreateUndef(type)};
          REQUIRE(expr != nullptr);
          CHECK(expr->getType() == ctx.DoubleTy);
          auto deref{clang::dyn_cast<clang::UnaryOperator>(expr)};
          REQUIRE(deref != nullptr);
          CHECK(deref->getOpcode() == clang::UO_Deref);
          IsNullPtrExprCheck(ctx, deref->getSubExpr()->IgnoreCasts());
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateCStyleCast") {
  SCENARIO("Create a CK_NullToPointer kind clang::CStyleCastExpr") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("a 0U literal") {
        auto lit{ast.CreateIntLit(llvm::APInt(32, 0))};
        REQUIRE(lit != nullptr);
        REQUIRE(lit->getType() == ctx.UnsignedIntTy);
        GIVEN("a void * type") {
          auto void_ptr_ty{ctx.VoidPtrTy};
          THEN("return an null-to-pointer cast to void *") {
            auto nullptr_cast{ast.CreateCStyleCast(void_ptr_ty, lit)};
            REQUIRE(nullptr_cast != nullptr);
            CHECK(nullptr_cast->getType() == void_ptr_ty);
            CHECK(nullptr_cast->getCastKind() ==
                  clang::CastKind::CK_NullToPointer);
          }
        }
      }
    }
  }

  SCENARIO("Create a CK_BitCast kind clang::CStyleCastExpr") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("a void * type expression") {
        auto void_ty_expr{ast.CreateNull()};
        REQUIRE(void_ty_expr != nullptr);
        REQUIRE(void_ty_expr->getType() == ctx.VoidPtrTy);
        GIVEN("a pointer type int *") {
          auto int_ptr_ty{ctx.getPointerType(ctx.IntTy)};
          THEN("return a bitcast to int *") {
            auto bitcast{ast.CreateCStyleCast(int_ptr_ty, void_ty_expr)};
            REQUIRE(bitcast != nullptr);
            CHECK(bitcast->getType() == int_ptr_ty);
            CHECK(bitcast->getCastKind() == clang::CastKind::CK_BitCast);
          }
        }
      }
    }
  }

  SCENARIO("Create a CK_IntegralCast kind clang::CStyleCastExpr") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("an integer literal") {
        auto lit{ast.CreateIntLit(llvm::APInt(8, 0xff))};
        REQUIRE(lit != nullptr);
        REQUIRE(lit->getType() == ctx.UnsignedIntTy);
        GIVEN("a unsigned long int type") {
          auto ulong_ty{ctx.UnsignedLongTy};
          THEN("return an integeral cast to unsigned long int") {
            auto intcast{ast.CreateCStyleCast(ulong_ty, lit)};
            REQUIRE(intcast != nullptr);
            CHECK(intcast->getType() == ulong_ty);
            CHECK(intcast->getCastKind() == clang::CastKind::CK_IntegralCast);
          }
        }
      }
    }
  }

  SCENARIO("Create a CK_PointerToIntegral kind clang::CStyleCastExpr") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("a null pointer expression") {
        auto null{ast.CreateNull()};
        REQUIRE(null != nullptr);
        GIVEN("an unsigned int type") {
          auto int_ty{ctx.UnsignedIntTy};
          THEN("return an pointer-to-integral cast to unsigned int") {
            auto ptr2int_cast{ast.CreateCStyleCast(int_ty, null)};
            REQUIRE(ptr2int_cast != nullptr);
            CHECK(ptr2int_cast->getType() == int_ty);
            CHECK(ptr2int_cast->getCastKind() ==
                  clang::CastKind::CK_PointerToIntegral);
          }
        }
      }
    }
  }

  SCENARIO("Create a CK_IntegralToPointer kind clang::CStyleCastExpr") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("an integer literal") {
        auto lit{ast.CreateIntLit(llvm::APInt(16, 0xbeef))};
        REQUIRE(lit != nullptr);
        GIVEN("a unsigned int * type") {
          auto uint_ptr_ty{ctx.getPointerType(ctx.UnsignedIntTy)};
          THEN("return an integral-to-pointer cast to unsigned int *") {
            auto int2ptr_cast{ast.CreateCStyleCast(uint_ptr_ty, lit)};
            REQUIRE(int2ptr_cast != nullptr);
            CHECK(int2ptr_cast->getType() == uint_ptr_ty);
            CHECK(int2ptr_cast->getCastKind() ==
                  clang::CastKind::CK_IntegralToPointer);
          }
        }
      }
    }
  }

  SCENARIO("Create a CK_FloatingCast kind clang::CStyleCastExpr") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("a float type literal") {
        auto lit{ast.CreateFPLit(llvm::APFloat(float(3.14)))};
        REQUIRE(lit != nullptr);
        GIVEN("a double type") {
          auto double_ty{ctx.DoubleTy};
          THEN("return a floating cast to double") {
            auto fp_cast{ast.CreateCStyleCast(double_ty, lit)};
            REQUIRE(fp_cast != nullptr);
            CHECK(fp_cast->getType() == double_ty);
            CHECK(fp_cast->getCastKind() == clang::CastKind::CK_FloatingCast);
          }
        }
      }
    }
  }

  SCENARIO("Create a CK_IntegralToFloating kind clang::CStyleCastExpr") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("an integer literal") {
        auto lit{ast.CreateIntLit(llvm::APInt(16, 0xdead))};
        REQUIRE(lit != nullptr);
        GIVEN("a float type") {
          auto float_ty{ctx.FloatTy};
          THEN("return a integral-to-floating cast to float") {
            auto int2fp_cast{ast.CreateCStyleCast(float_ty, lit)};
            REQUIRE(int2fp_cast != nullptr);
            CHECK(int2fp_cast->getType() == float_ty);
            CHECK(int2fp_cast->getCastKind() ==
                  clang::CastKind::CK_IntegralToFloating);
          }
        }
      }
    }
  }

  SCENARIO("Create a CK_FloatingToIntegral clang::CStyleCastExpr") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("a double type literal") {
        auto lit{ast.CreateFPLit(llvm::APFloat(double(3.14)))};
        REQUIRE(lit != nullptr);
        GIVEN("an unsigned long int type") {
          auto ulong_ty{ctx.UnsignedLongTy};
          THEN("return a floating-to-integral cast to unsigned long int") {
            auto fp2int_cast{ast.CreateCStyleCast(ulong_ty, lit)};
            REQUIRE(fp2int_cast != nullptr);
            CHECK(fp2int_cast->getType() == ulong_ty);
            CHECK(fp2int_cast->getCastKind() ==
                  clang::CastKind::CK_FloatingToIntegral);
          }
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateVarDecl") {
  SCENARIO("Create a global clang::VarDecl") {
    GIVEN("An empty translation unit") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      THEN("return an unsigned int global variable my_var") {
        auto type{ctx.UnsignedIntTy};
        auto vardecl{ast.CreateVarDecl(tudecl, type, "my_var")};
        REQUIRE(vardecl != nullptr);
        CHECK(vardecl->getType() == type);
        CHECK(vardecl->hasGlobalStorage());
        CHECK(!vardecl->isStaticLocal());
        CHECK(vardecl->getName() == "my_var");
        CHECK(tudecl->containsDecl(vardecl));
      }
    }
  }

  SCENARIO("Create a function local clang::VarDecl") {
    GIVEN("An empty function definition f") {
      auto unit{GetASTUnit("void f(){};")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      auto fdecl{GetDecl<clang::FunctionDecl>(tudecl, "f")};
      REQUIRE(fdecl->hasBody());
      THEN("return a int variable my_var declared in f") {
        auto type{ctx.IntTy};
        auto vardecl{ast.CreateVarDecl(fdecl, type, "my_var")};
        REQUIRE(vardecl != nullptr);
        CHECK(vardecl->getType() == type);
        CHECK(vardecl->isLocalVarDecl());
        CHECK(vardecl->getName() == "my_var");
        CHECK(fdecl->containsDecl(vardecl));
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateDeclStmt") {
  SCENARIO("Create a clang::DeclStmt to a function local clang::VarDecl") {
    GIVEN("a funtion with a local variable declaration") {
      auto unit{GetASTUnit("void f(){}")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      auto fdecl{GetDecl<clang::FunctionDecl>(tudecl, "f")};
      REQUIRE(fdecl->hasBody());
      GIVEN("a int variable my_var declared in f") {
        auto vardecl{ast.CreateVarDecl(fdecl, ctx.IntTy, "my_var")};
        THEN("return a declaration statement for my_var") {
          auto declstmt{ast.CreateDeclStmt(vardecl)};
          REQUIRE(declstmt != nullptr);
          CHECK(declstmt->isSingleDecl());
          CHECK(declstmt->getSingleDecl() == vardecl);
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateDeclRef") {
  SCENARIO("Create a clang::DeclRef to a global clang::VarDecl") {
    GIVEN("a declaration of global variable a") {
      auto unit{GetASTUnit("int a;")};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{unit->getASTContext().getTranslationUnitDecl()};
      auto vardecl{GetDecl<clang::VarDecl>(tudecl, "a")};
      THEN("return a declaration reference to a") {
        auto declref{ast.CreateDeclRef(vardecl)};
        REQUIRE(declref != nullptr);
        CHECK(declref->getDecl() == vardecl);
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateUnaryOperator") {
  SCENARIO("Create a pointer dereference operation") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("a void * type expression e") {
        auto expr{ast.CreateNull()};
        REQUIRE(expr != nullptr);
        THEN("return *e") {
          auto deref{ast.CreateDeref(expr)};
          REQUIRE(deref != nullptr);
          CHECK(deref->getSubExpr() == expr);
          CHECK(deref->getType() == expr->getType()->getPointeeType());
          CHECK(deref->getOpcode() == clang::UO_Deref);
        }
      }
    }
  }

  SCENARIO("Create a pointer address-of operation") {
    GIVEN("A declaration of global variable a") {
      auto unit{GetASTUnit("int a;")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      auto vardecl{GetDecl<clang::VarDecl>(tudecl, "a")};
      GIVEN("a reference to a") {
        auto declref{ast.CreateDeclRef(vardecl)};
        REQUIRE(declref != nullptr);
        THEN("return &a") {
          auto addrof{ast.CreateAddrOf(declref)};
          REQUIRE(addrof != nullptr);
          CHECK(addrof->getSubExpr() == declref);
          CHECK(addrof->getType() == ctx.getPointerType(vardecl->getType()));
          CHECK(addrof->getOpcode() == clang::UO_AddrOf);
        }
      }
    }
  }

  SCENARIO("Create a logical negation operation") {
    GIVEN("A declaration of global variable a") {
      auto unit{GetASTUnit("int a;")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      auto vardecl{GetDecl<clang::VarDecl>(tudecl, "a")};
      GIVEN("a reference to a") {
        auto declref{ast.CreateDeclRef(vardecl)};
        REQUIRE(declref != nullptr);
        THEN("return !a") {
          auto lnot{ast.CreateLNot(declref)};
          REQUIRE(lnot != nullptr);
          CHECK(lnot->getSubExpr() == declref);
          CHECK(lnot->getType() == ctx.IntTy);
          CHECK(lnot->getOpcode() == clang::UO_LNot);
        }
      }
    }
  }

  SCENARIO("Create a pointer bitwise negation operation") {
    GIVEN("A declaration of global variable a") {
      auto unit{GetASTUnit("int a;")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      auto vardecl{GetDecl<clang::VarDecl>(tudecl, "a")};
      GIVEN("a reference to a") {
        auto declref{ast.CreateDeclRef(vardecl)};
        REQUIRE(declref != nullptr);
        THEN("return ~a") {
          auto bnot{ast.CreateNot(declref)};
          REQUIRE(bnot != nullptr);
          CHECK(bnot->getSubExpr() == declref);
          CHECK(bnot->getType() == declref->getType());
          CHECK(bnot->getOpcode() == clang::UO_Not);
        }
      }
    }
  }
}

// TEST_SUITE("ASTBuilder::CreateImplicitCast") {
//   SCENARIO("Create a CK_LValueToRValue clang::CreateImplicitCast") {}
//   SCENARIO("Create a CK_FunctionToPointerDecay
//   clang::CreateImplicitCast")
//   {} SCENARIO("Create a CK_ArrayToPointerDecay
//   clang::CreateImplicitCast")
//   {}
// }