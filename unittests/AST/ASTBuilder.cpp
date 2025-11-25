/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/ASTBuilder.h"

#include "Util.h"

namespace {

template <typename T>
static clang::DeclRefExpr *GetDeclRef(rellic::ASTBuilder &ast,
                                      clang::DeclContext *decl_ctx,
                                      const std::string &name) {
  auto ref{ast.CreateDeclRef(GetDecl<T>(decl_ctx, name))};
  REQUIRE(ref != nullptr);
  return ref;
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
        llvm::APInt api(1U, UINT64_C(0), /*isSigned=*/false);
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
        llvm::APInt api(8U, UINT64_C(42), /*isSigned=*/false);
        THEN("return a unsigned int typed integer literal") {
          auto lit{ast.CreateIntLit(api)};
          REQUIRE(lit != nullptr);
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
        llvm::APInt api(16U, UINT64_C(42), /*isSigned=*/false);
        THEN("return a unsigned int typed integer literal") {
          auto lit{ast.CreateIntLit(api)};
          REQUIRE(lit != nullptr);
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
        llvm::APInt api(32U, UINT64_C(42), /*isSigned=*/false);
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
        llvm::APInt api(64U, UINT64_C(42), /*isSigned=*/false);
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
        llvm::APInt api(128U, UINT64_C(42), /*isSigned=*/false);
        THEN("return a _uint128_t typed integer literal") {
          auto lit{ast.CreateIntLit(api)};
          REQUIRE(lit != nullptr);
          CHECK(clang::isa<clang::IntegerLiteral>(lit));
          CHECK(lit->getType() == ctx.UnsignedInt128Ty);
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
      GIVEN("c char value") {
        auto lit{ast.CreateCharLit('x')};
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
  SCENARIO("Create clang::Expr to stand in for LLVM's 'undef' values") {
    GIVEN("Empty clang::ASTContext") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("an unsigned integer type") {
        auto type{ctx.UnsignedIntTy};
        THEN(
            "return a value that satisfied LLVM's undef semantics (aka "
            "anything)") {
          auto expr{ast.CreateUndefInteger(type)};
          REQUIRE(expr != nullptr);
          CHECK(expr->getType() == ctx.UnsignedIntTy);
        }
      }
      GIVEN("an arbitrary type t") {
        auto type{ctx.DoubleTy};
        THEN("return an undef pointer (we use null pointers) of type t") {
          auto expr{ast.CreateUndefPointer(type)};
          REQUIRE(expr != nullptr);
          auto double_ptr_ty{ctx.getPointerType(ctx.DoubleTy)};
          CHECK(expr->getType() == double_ptr_ty);
          IsNullPtrExprCheck(ctx, expr->IgnoreCasts());
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
    GIVEN("Empty translation unit") {
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
      }
    }
  }

  SCENARIO("Create a function local clang::VarDecl") {
    GIVEN("Function definition void f(){}") {
      auto unit{GetASTUnit("void f(){}")};
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
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateFunctionDecl") {
  SCENARIO("Create a clang::FunctionDecl") {
    GIVEN("Empty translation unit") {
      auto unit{GetASTUnit("")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      THEN("return a function declaration void f()") {
        auto type{ctx.getFunctionType(
            ctx.VoidTy, {}, clang::FunctionProtoType::ExtProtoInfo())};
        auto fdecl{ast.CreateFunctionDecl(tudecl, type, "f")};
        REQUIRE(fdecl != nullptr);
        CHECK(fdecl->getName() == "f");
        CHECK(fdecl->getType() == type);
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateParamDecl") {
  SCENARIO("Create a clang::ParmVarDecl") {
    GIVEN("Function definition void f(){}") {
      auto unit{GetASTUnit("void f(){}")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      auto fdecl{GetDecl<clang::FunctionDecl>(tudecl, "f")};
      THEN("return a parameter declaration p of void f(int p){}") {
        auto pdecl{ast.CreateParamDecl(fdecl, ctx.IntTy, "p")};
        REQUIRE(pdecl != nullptr);
        CHECK(pdecl->getName() == "p");
        CHECK(pdecl->getType() == ctx.IntTy);
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateStructDecl") {
  SCENARIO("Create a clang::RecordDecl") {
    GIVEN("Empty translation unit") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      THEN("return an empty structure s") {
        auto record_decl{ast.CreateStructDecl(tudecl, "s")};
        REQUIRE(record_decl != nullptr);
        CHECK(record_decl->getName() == "s");
        CHECK(record_decl->getTagKind() ==
              clang::RecordDecl::TagKind::TTK_Struct);
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateUnionDecl") {
  SCENARIO("Create a clang::RecordDecl") {
    GIVEN("Empty translation unit") {
      auto unit{GetASTUnit()};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      THEN("return an empty union u") {
        auto record_decl{ast.CreateUnionDecl(tudecl, "u")};
        REQUIRE(record_decl != nullptr);
        CHECK(record_decl->getName() == "u");
        CHECK(record_decl->getTagKind() ==
              clang::RecordDecl::TagKind::TTK_Union);
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateFieldDecl") {
  SCENARIO("Create a clang::FieldDecl") {
    GIVEN("Structure definition s") {
      auto unit{GetASTUnit("struct s{};")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      auto record_decl{GetDecl<clang::RecordDecl>(tudecl, "s")};
      REQUIRE(record_decl->field_empty());
      THEN("return structure s with member int f") {
        auto type{ctx.IntTy};
        auto field_decl{ast.CreateFieldDecl(record_decl, type, "f")};
        REQUIRE(field_decl != nullptr);
        CHECK(field_decl->getType() == type);
        CHECK(field_decl->getName() == "f");
      }
    }
  }

  SCENARIO("Create a bitfield clang::FieldDecl") {
    GIVEN("Structure definition s") {
      auto unit{GetASTUnit("struct s{};")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      auto record_decl{GetDecl<clang::RecordDecl>(tudecl, "s")};
      REQUIRE(record_decl->field_empty());
      THEN("return structure s with member int f with size 3") {
        auto type{ctx.IntTy};
        auto field_decl{ast.CreateFieldDecl(record_decl, type, "f", 3)};
        REQUIRE(field_decl != nullptr);
        CHECK(field_decl->getType() == type);
        CHECK(field_decl->getName() == "f");
        CHECK(field_decl->getBitWidthValue(ctx) == 3);
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateDeclStmt") {
  SCENARIO("Create a clang::DeclStmt to a function local clang::VarDecl") {
    GIVEN("Funtion with a local variable declaration") {
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
    GIVEN("Global variable int a;") {
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

TEST_SUITE("ASTBuilder::CreateParen") {
  SCENARIO("Create parenthesis around an expression") {
    GIVEN("Global variable int a;") {
      auto unit{GetASTUnit("int a;")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      GIVEN("a reference to a") {
        auto declref{GetDeclRef<clang::VarDecl>(ast, tudecl, "a")};
        THEN("return (a)") {
          auto paren{ast.CreateParen(declref)};
          REQUIRE(paren != nullptr);
          CHECK(paren->getSubExpr() == declref);
          CHECK(paren->getType() == declref->getType());
        }
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
    GIVEN("Global variable int a;") {
      auto unit{GetASTUnit("int a;")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      GIVEN("a reference to a") {
        auto declref{GetDeclRef<clang::VarDecl>(ast, tudecl, "a")};
        THEN("return &a") {
          auto addrof{ast.CreateAddrOf(declref)};
          REQUIRE(addrof != nullptr);
          auto subexpr{addrof->getSubExpr()};
          CHECK(subexpr == declref);
          CHECK(addrof->getType() == ctx.getPointerType(declref->getType()));
          CHECK(addrof->getOpcode() == clang::UO_AddrOf);
        }
      }
    }
  }

  SCENARIO("Create a logical negation operation") {
    GIVEN("Global variable int a;") {
      auto unit{GetASTUnit("int a;")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      GIVEN("a reference to a") {
        auto declref{GetDeclRef<clang::VarDecl>(ast, tudecl, "a")};
        THEN("return !a") {
          auto lnot{ast.CreateLNot(declref)};
          REQUIRE(lnot != nullptr);
          auto subexpr{lnot->getSubExpr()};
          CHECK(clang::isa<clang::ImplicitCastExpr>(subexpr));
          CHECK(subexpr->Classify(ctx).isRValue());
          CHECK(subexpr->IgnoreImpCasts() == declref);
          CHECK(lnot->getType() == ctx.IntTy);
          CHECK(lnot->getOpcode() == clang::UO_LNot);
        }
      }
    }
  }

  SCENARIO("Create a pointer bitwise negation operation") {
    GIVEN("Global variable int a;") {
      auto unit{GetASTUnit("int a;")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      GIVEN("a reference to a") {
        auto declref{GetDeclRef<clang::VarDecl>(ast, tudecl, "a")};
        THEN("return ~a") {
          auto bnot{ast.CreateNot(declref)};
          REQUIRE(bnot != nullptr);
          auto subexpr{bnot->getSubExpr()};
          CHECK(clang::isa<clang::ImplicitCastExpr>(subexpr));
          CHECK(subexpr->Classify(ctx).isRValue());
          CHECK(subexpr->IgnoreImpCasts() == declref);
          CHECK(bnot->getType() == declref->getType());
          CHECK(bnot->getOpcode() == clang::UO_Not);
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateBinaryOp") {
  static void CheckBinOp(clang::BinaryOperator * op, clang::Expr * lhs,
                         clang::Expr * rhs, clang::QualType type) {
    REQUIRE(op != nullptr);
    CHECK(op->getLHS()->IgnoreImpCasts() == lhs);
    CHECK(op->getRHS()->IgnoreImpCasts() == rhs);
    CHECK(op->getType() == type);
  }

  static void CheckRelOp(clang::BinaryOperator * op, clang::Expr * lhs,
                         clang::Expr * rhs, clang::QualType type) {
    CheckBinOp(op, lhs, rhs, type);
    CHECK(op->getLHS()->getType() == op->getRHS()->getType());
  }

  SCENARIO("Create logical and comparison operations") {
    GIVEN("Global variables int a, b; short c; long d;") {
      auto unit{GetASTUnit("int a,b; short c; long d;")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      GIVEN("references to a, b, c and d") {
        auto ref_a{GetDeclRef<clang::VarDecl>(ast, tudecl, "a")};
        auto ref_b{GetDeclRef<clang::VarDecl>(ast, tudecl, "b")};
        auto ref_c{GetDeclRef<clang::VarDecl>(ast, tudecl, "c")};
        auto ref_d{GetDeclRef<clang::VarDecl>(ast, tudecl, "d")};
        // BO_LAnd
        THEN("return a && b") {
          auto land{ast.CreateLAnd(ref_a, ref_b)};
          CheckBinOp(land, ref_a, ref_b, ctx.IntTy);
          CHECK(land->getLHS()->getType() == land->getRHS()->getType());
        }
        THEN("return a && c") {
          auto land{ast.CreateLAnd(ref_a, ref_c)};
          CheckBinOp(land, ref_a, ref_c, ctx.IntTy);
          CHECK(land->getLHS()->getType() == land->getRHS()->getType());
        }
        THEN("return d && b") {
          auto land{ast.CreateLAnd(ref_d, ref_b)};
          CheckBinOp(land, ref_d, ref_b, ctx.IntTy);
        }
        // BO_LOr
        THEN("return a || b") {
          auto lor{ast.CreateLOr(ref_a, ref_b)};
          CheckBinOp(lor, ref_a, ref_b, ctx.IntTy);
          CHECK(lor->getLHS()->getType() == lor->getRHS()->getType());
        }
        THEN("return a || c") {
          auto lor{ast.CreateLOr(ref_a, ref_c)};
          CheckBinOp(lor, ref_a, ref_c, ctx.IntTy);
          CHECK(lor->getLHS()->getType() == lor->getRHS()->getType());
        }
        THEN("return d || b") {
          auto lor{ast.CreateLOr(ref_d, ref_b)};
          CheckBinOp(lor, ref_d, ref_b, ctx.IntTy);
        }
        // BO_EQ
        THEN("return a == b") {
          CheckRelOp(ast.CreateEQ(ref_a, ref_b), ref_a, ref_b, ctx.IntTy);
        }
        THEN("return a == c") {
          CheckRelOp(ast.CreateEQ(ref_a, ref_c), ref_a, ref_c, ctx.IntTy);
        }
        THEN("return d == b") {
          CheckRelOp(ast.CreateEQ(ref_d, ref_b), ref_d, ref_b, ctx.IntTy);
        }
        // BO_NE
        THEN("return a != b") {
          CheckRelOp(ast.CreateNE(ref_a, ref_b), ref_a, ref_b, ctx.IntTy);
        }
        THEN("return a != c") {
          CheckRelOp(ast.CreateNE(ref_a, ref_c), ref_a, ref_c, ctx.IntTy);
        }
        THEN("return d != b") {
          CheckRelOp(ast.CreateNE(ref_d, ref_b), ref_d, ref_b, ctx.IntTy);
        }
        // BO_GE
        THEN("return a >= b") {
          CheckRelOp(ast.CreateGE(ref_a, ref_b), ref_a, ref_b, ctx.IntTy);
        }
        THEN("return a >= c") {
          CheckRelOp(ast.CreateGE(ref_a, ref_c), ref_a, ref_c, ctx.IntTy);
        }
        THEN("return d >= b") {
          CheckRelOp(ast.CreateGE(ref_d, ref_b), ref_d, ref_b, ctx.IntTy);
        }
        // BO_GT
        THEN("return a > b") {
          CheckRelOp(ast.CreateGT(ref_a, ref_b), ref_a, ref_b, ctx.IntTy);
        }
        THEN("return a > c") {
          CheckRelOp(ast.CreateGT(ref_a, ref_c), ref_a, ref_c, ctx.IntTy);
        }
        THEN("return d > b") {
          CheckRelOp(ast.CreateGT(ref_d, ref_b), ref_d, ref_b, ctx.IntTy);
        }
        // BO_LE
        THEN("return a <= b") {
          CheckRelOp(ast.CreateLE(ref_a, ref_b), ref_a, ref_b, ctx.IntTy);
        }
        THEN("return a <= c") {
          CheckRelOp(ast.CreateLE(ref_a, ref_c), ref_a, ref_c, ctx.IntTy);
        }
        THEN("return d <= b") {
          CheckRelOp(ast.CreateLE(ref_d, ref_b), ref_d, ref_b, ctx.IntTy);
        }
        // BO_LT
        THEN("return a < b") {
          CheckRelOp(ast.CreateLT(ref_a, ref_b), ref_a, ref_b, ctx.IntTy);
        }
        THEN("return a < c") {
          CheckRelOp(ast.CreateLT(ref_a, ref_c), ref_a, ref_c, ctx.IntTy);
        }
        THEN("return d < b") {
          CheckRelOp(ast.CreateLT(ref_d, ref_b), ref_d, ref_b, ctx.IntTy);
        }
      }
    }
  }

  static void CheckArithOp(clang::BinaryOperator * op, clang::Expr * lhs,
                           clang::Expr * rhs, clang::QualType type) {
    CheckRelOp(op, lhs, rhs, type);
  }

  SCENARIO("Create bitwise arithmetic operations") {
    GIVEN("Global variables int a, b; short c; long d;") {
      auto unit{GetASTUnit("int a,b; short c; long d;")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      GIVEN("references to a, b, c and d") {
        auto ref_a{GetDeclRef<clang::VarDecl>(ast, tudecl, "a")};
        auto ref_b{GetDeclRef<clang::VarDecl>(ast, tudecl, "b")};
        auto ref_c{GetDeclRef<clang::VarDecl>(ast, tudecl, "c")};
        auto ref_d{GetDeclRef<clang::VarDecl>(ast, tudecl, "d")};
        // BO_And
        THEN("return a & b") {
          CheckArithOp(ast.CreateAnd(ref_a, ref_b), ref_a, ref_b, ctx.IntTy);
        }
        THEN("return a & c") {
          CheckArithOp(ast.CreateAnd(ref_a, ref_c), ref_a, ref_c, ctx.IntTy);
        }
        THEN("return d & b") {
          CheckBinOp(ast.CreateAnd(ref_d, ref_b), ref_d, ref_b, ctx.LongTy);
        }
        // BO_Or
        THEN("return a | b") {
          CheckArithOp(ast.CreateOr(ref_a, ref_b), ref_a, ref_b, ctx.IntTy);
        }
        THEN("return a | c") {
          CheckArithOp(ast.CreateOr(ref_a, ref_c), ref_a, ref_c, ctx.IntTy);
        }
        THEN("return d | b") {
          CheckBinOp(ast.CreateOr(ref_d, ref_b), ref_d, ref_b, ctx.LongTy);
        }
        // BO_Xor
        THEN("return a ^ b") {
          CheckArithOp(ast.CreateXor(ref_a, ref_b), ref_a, ref_b, ctx.IntTy);
        }
        THEN("return a ^ c") {
          CheckArithOp(ast.CreateXor(ref_a, ref_c), ref_a, ref_c, ctx.IntTy);
        }
        THEN("return d ^ b") {
          CheckBinOp(ast.CreateXor(ref_d, ref_b), ref_d, ref_b, ctx.LongTy);
        }
        // BO_Shl
        THEN("return a << b") {
          CheckArithOp(ast.CreateShl(ref_a, ref_b), ref_a, ref_b, ctx.IntTy);
        }
        THEN("return a << c") {
          CheckArithOp(ast.CreateShl(ref_a, ref_c), ref_a, ref_c, ctx.IntTy);
        }
        THEN("return d << b") {
          CheckBinOp(ast.CreateShl(ref_d, ref_b), ref_d, ref_b, ctx.LongTy);
        }
        // BO_Shr
        THEN("return a >> b") {
          CheckArithOp(ast.CreateShr(ref_a, ref_b), ref_a, ref_b, ctx.IntTy);
        }
        THEN("return a >> c") {
          CheckArithOp(ast.CreateShr(ref_a, ref_c), ref_a, ref_c, ctx.IntTy);
        }
        THEN("return d >> b") {
          CheckBinOp(ast.CreateShr(ref_d, ref_b), ref_d, ref_b, ctx.LongTy);
        }
      }
    }
  }
  SCENARIO("Create integer arithmetic operations") {
    GIVEN("Global variables int a, b; short c; long d;") {
      auto unit{GetASTUnit("int a,b; short c; long d;")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      GIVEN("references to a, b, c and d") {
        auto ref_a{GetDeclRef<clang::VarDecl>(ast, tudecl, "a")};
        auto ref_b{GetDeclRef<clang::VarDecl>(ast, tudecl, "b")};
        auto ref_c{GetDeclRef<clang::VarDecl>(ast, tudecl, "c")};
        auto ref_d{GetDeclRef<clang::VarDecl>(ast, tudecl, "d")};
        // BO_Add
        THEN("return a + b") {
          CheckArithOp(ast.CreateAdd(ref_a, ref_b), ref_a, ref_b, ctx.IntTy);
        }
        THEN("return a + c") {
          CheckArithOp(ast.CreateAdd(ref_a, ref_c), ref_a, ref_c, ctx.IntTy);
        }
        THEN("return d + b") {
          CheckBinOp(ast.CreateAdd(ref_d, ref_b), ref_d, ref_b, ctx.LongTy);
        }
        // BO_Add
        THEN("return a - b") {
          CheckArithOp(ast.CreateSub(ref_a, ref_b), ref_a, ref_b, ctx.IntTy);
        }
        THEN("return a - c") {
          CheckArithOp(ast.CreateSub(ref_a, ref_c), ref_a, ref_c, ctx.IntTy);
        }
        THEN("return d - b") {
          CheckBinOp(ast.CreateSub(ref_d, ref_b), ref_d, ref_b, ctx.LongTy);
        }
        // BO_Mul
        THEN("return a * b") {
          CheckArithOp(ast.CreateMul(ref_a, ref_b), ref_a, ref_b, ctx.IntTy);
        }
        THEN("return a * c") {
          CheckArithOp(ast.CreateMul(ref_a, ref_c), ref_a, ref_c, ctx.IntTy);
        }
        THEN("return d * b") {
          CheckBinOp(ast.CreateMul(ref_d, ref_b), ref_d, ref_b, ctx.LongTy);
        }
        // BO_Div
        THEN("return a / b") {
          CheckArithOp(ast.CreateDiv(ref_a, ref_b), ref_a, ref_b, ctx.IntTy);
        }
        THEN("return a / c") {
          CheckArithOp(ast.CreateDiv(ref_a, ref_c), ref_a, ref_c, ctx.IntTy);
        }
        THEN("return d / b") {
          CheckBinOp(ast.CreateDiv(ref_d, ref_b), ref_d, ref_b, ctx.LongTy);
        }
        // BO_Rem
        THEN("return a % b") {
          CheckArithOp(ast.CreateRem(ref_a, ref_b), ref_a, ref_b, ctx.IntTy);
        }
        THEN("return a % c") {
          CheckArithOp(ast.CreateRem(ref_a, ref_c), ref_a, ref_c, ctx.IntTy);
        }
        THEN("return d % b") {
          CheckBinOp(ast.CreateRem(ref_d, ref_b), ref_d, ref_b, ctx.LongTy);
        }
        // BO_Assign
        THEN("return a = b") {
          CheckArithOp(ast.CreateAssign(ref_a, ref_b), ref_a, ref_b, ctx.IntTy);
        }
        THEN("return a = c") {
          CheckArithOp(ast.CreateAssign(ref_a, ref_c), ref_a, ref_c, ctx.IntTy);
        }
        THEN("return d = b") {
          CheckBinOp(ast.CreateAssign(ref_d, ref_b), ref_d, ref_b, ctx.LongTy);
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateConditional") {
  SCENARIO("Create a ternary conditional operation") {
    GIVEN("Global variables int a,b,c;") {
      auto unit{GetASTUnit("int a,b,c;")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      GIVEN("references to a, b and c") {
        auto ref_a{GetDeclRef<clang::VarDecl>(ast, tudecl, "a")};
        auto ref_b{GetDeclRef<clang::VarDecl>(ast, tudecl, "b")};
        auto ref_c{GetDeclRef<clang::VarDecl>(ast, tudecl, "c")};
        THEN("return a ? b : c") {
          auto ite{ast.CreateConditional(ref_a, ref_b, ref_c)};
          REQUIRE(ite != nullptr);
          CHECK(ite->getType() == ctx.IntTy);
          CHECK(ite->getCond()->IgnoreImpCasts() == ref_a);
          CHECK(ite->getLHS()->IgnoreImpCasts() == ref_b);
          CHECK(ite->getRHS()->IgnoreImpCasts() == ref_c);
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateArraySub") {
  SCENARIO("Create an array subscript operation") {
    GIVEN("Global variable const char a[] = \"Hello\";") {
      auto unit{GetASTUnit("const char a[] = \"Hello\";")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      GIVEN("reference to a and a 1U literal") {
        auto ref_a{GetDeclRef<clang::VarDecl>(ast, tudecl, "a")};
        auto lit{ast.CreateIntLit(llvm::APInt(32U, 1U))};
        REQUIRE(lit != nullptr);
        THEN("return a[1U]") {
          auto array_sub{ast.CreateArraySub(ref_a, lit)};
          CHECK(array_sub != nullptr);
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateCall") {
  SCENARIO("Create a call operation") {
    GIVEN("Function declaration void f(int a, int b);") {
      auto unit{GetASTUnit("void f(int a, int b);")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      auto func{GetDeclRef<clang::FunctionDecl>(ast, tudecl, "f")};
      GIVEN("integer literals 4U and 1U") {
        auto lit_4{ast.CreateIntLit(llvm::APInt(32U, 4U))};
        auto lit_1{ast.CreateIntLit(llvm::APInt(32U, 1U))};
        REQUIRE(lit_4 != nullptr);
        REQUIRE(lit_1 != nullptr);
        THEN("return f(4U, 1U)") {
          std::vector<clang::Expr *> args{lit_4, lit_1};
          auto call{ast.CreateCall(func, args)};
          CHECK(call != nullptr);
          CHECK(call->getCallee()->IgnoreImpCasts() == func);
          CHECK(call->getArg(0)->IgnoreImpCasts() == lit_4);
          CHECK(call->getArg(1)->IgnoreImpCasts() == lit_1);
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateFieldAcc") {
  SCENARIO("Create a structure field access operations") {
    GIVEN("Structure declaration struct pair{int a; int b;}; struct pair p;") {
      auto unit{GetASTUnit("struct pair{int a; int b;}; struct pair p;")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      auto ref{GetDeclRef<clang::VarDecl>(ast, tudecl, "p")};
      auto sdecl{GetDecl<clang::RecordDecl>(tudecl, "pair")};
      THEN("return p.a") {
        auto field_a{GetDecl<clang::FieldDecl>(sdecl, "a")};
        auto member{ast.CreateDot(ref, field_a)};
        CHECK(member != nullptr);
      }
      THEN("return (&p)->b") {
        auto field_b{GetDecl<clang::FieldDecl>(sdecl, "b")};
        auto member{ast.CreateArrow(ast.CreateAddrOf(ref), field_b)};
        CHECK(member != nullptr);
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateInitList") {
  SCENARIO("Create a initializer list expression") {
    GIVEN("Global variables int a; short b; char c;") {
      auto unit{GetASTUnit("int a; short b; char c;")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      GIVEN("references to a, b and c") {
        auto ref_a{GetDeclRef<clang::VarDecl>(ast, tudecl, "a")};
        auto ref_b{GetDeclRef<clang::VarDecl>(ast, tudecl, "b")};
        auto ref_c{GetDeclRef<clang::VarDecl>(ast, tudecl, "c")};
        THEN("return {a, b, c}") {
          std::vector<clang::Expr *> exprs{ref_a, ref_b, ref_c};
          auto init_list{ast.CreateInitList(exprs)};
          REQUIRE(init_list != nullptr);
          CHECK(init_list->isSemanticForm());
          CHECK(init_list->end() - init_list->begin() == 3U);
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateCompoundLit") {
  SCENARIO("Create a compound literal expression") {
    GIVEN("An empty initializer list expression") {
      auto unit{GetASTUnit("")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      std::vector<clang::Expr *> exprs;
      auto init_list{ast.CreateInitList(exprs)};
      GIVEN("int[] type") {
        auto type{ctx.getIncompleteArrayType(
            ctx.IntTy, clang::ArrayType::ArraySizeModifier(), 0)};
        THEN("return (int[]){}") {
          auto comp_lit{ast.CreateCompoundLit(type, init_list)};
          REQUIRE(comp_lit != nullptr);
          CHECK(comp_lit->getTypeSourceInfo()->getType() == type);
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateCompoundStmt") {
  SCENARIO("Create a compound statement") {
    GIVEN("Global variables int a; short b; char c;") {
      auto unit{GetASTUnit("int a; short b; char c;")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      GIVEN("references to a, b and c") {
        auto ref_a{GetDeclRef<clang::VarDecl>(ast, tudecl, "a")};
        auto ref_b{GetDeclRef<clang::VarDecl>(ast, tudecl, "b")};
        auto ref_c{GetDeclRef<clang::VarDecl>(ast, tudecl, "c")};
        THEN("return {a+b; b*c; c/a;}") {
          std::vector<clang::Stmt *> stmts;
          stmts.push_back(ast.CreateAdd(ref_a, ref_b));
          stmts.push_back(ast.CreateMul(ref_b, ref_c));
          stmts.push_back(ast.CreateDiv(ref_c, ref_a));
          auto compound{ast.CreateCompoundStmt(stmts)};
          REQUIRE(compound != nullptr);
          CHECK(compound->size() == stmts.size());
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateIf") {
  SCENARIO("Create an if statement") {
    GIVEN("Empty translation unit") {
      auto unit{GetASTUnit()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("Empty compound statement and 1U literal") {
        std::vector<clang::Stmt *> stmts;
        auto body{ast.CreateCompoundStmt(stmts)};
        auto cond{ast.CreateTrue()};
        THEN("return if(1U){};") {
          auto if_stmt{ast.CreateIf(cond, body)};
          REQUIRE(if_stmt != nullptr);
          CHECK(if_stmt->getCond() == cond);
          CHECK(if_stmt->getThen() == body);
          CHECK(if_stmt->getElse() == nullptr);
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateWhile") {
  SCENARIO("Create a while statement") {
    GIVEN("Empty translation unit") {
      auto unit{GetASTUnit()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("Empty compound statement and 1U literal") {
        std::vector<clang::Stmt *> stmts;
        auto body{ast.CreateCompoundStmt(stmts)};
        auto cond{ast.CreateTrue()};
        THEN("return while(1U){};") {
          auto while_stmt{ast.CreateWhile(cond, body)};
          REQUIRE(while_stmt != nullptr);
          CHECK(while_stmt->getCond() == cond);
          CHECK(while_stmt->getBody() == body);
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateDo") {
  SCENARIO("Create a do statement") {
    GIVEN("Empty translation unit") {
      auto unit{GetASTUnit()};
      rellic::ASTBuilder ast(*unit);
      GIVEN("Empty compound statement and 1U literal") {
        std::vector<clang::Stmt *> stmts;
        auto body{ast.CreateCompoundStmt(stmts)};
        auto cond{ast.CreateTrue()};
        THEN("return do{}while(1U);") {
          auto do_stmt{ast.CreateDo(cond, body)};
          REQUIRE(do_stmt != nullptr);
          CHECK(do_stmt->getCond() == cond);
          CHECK(do_stmt->getBody() == body);
        }
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateBreak") {
  SCENARIO("Create a break statement") {
    GIVEN("Empty translation unit") {
      auto unit{GetASTUnit()};
      rellic::ASTBuilder ast(*unit);
      THEN("return break;") {
        auto brk_stmt{ast.CreateBreak()};
        REQUIRE(brk_stmt != nullptr);
        CHECK(clang::isa<clang::BreakStmt>(brk_stmt));
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateReturn") {
  SCENARIO("Create a return statement") {
    GIVEN("Empty translation unit") {
      auto unit{GetASTUnit("")};
      rellic::ASTBuilder ast(*unit);
      THEN("return a return;") {
        auto ret_stmt{ast.CreateReturn()};
        REQUIRE(ret_stmt != nullptr);
        CHECK(clang::isa<clang::ReturnStmt>(ret_stmt));
      }
      THEN("return a return 1U;") {
        auto lit{ast.CreateTrue()};
        auto ret_stmt{ast.CreateReturn(lit)};
        REQUIRE(ret_stmt != nullptr);
        CHECK(clang::isa<clang::ReturnStmt>(ret_stmt));
        CHECK(ret_stmt->getRetValue() == lit);
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateNullStmt") {
  SCENARIO("Create a null statement") {
    GIVEN("Empty translation unit") {
      auto unit{GetASTUnit("")};
      rellic::ASTBuilder ast(*unit);
      THEN("return a ;") {
        auto null_stmt{ast.CreateNullStmt()};
        REQUIRE(null_stmt != nullptr);
        CHECK(clang::isa<clang::NullStmt>(null_stmt));
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateCaseStmt") {
  SCENARIO("Create a case statement") {
    GIVEN("Empty translation unit") {
      auto unit{GetASTUnit("")};
      rellic::ASTBuilder ast(*unit);
      THEN("return a case 0") {
        auto case_0{ast.CreateCaseStmt(ast.CreateIntLit(llvm::APInt(32, 0)))};
        REQUIRE(case_0 != nullptr);
        CHECK(clang::isa<clang::CaseStmt>(case_0));
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateDefaultStmt") {
  SCENARIO("Create a default statement") {
    GIVEN("Empty translation unit") {
      auto unit{GetASTUnit("")};
      rellic::ASTBuilder ast(*unit);
      THEN("return a default: break") {
        auto default_0{ast.CreateDefaultStmt(ast.CreateBreak())};
        REQUIRE(default_0 != nullptr);
        CHECK(clang::isa<clang::DefaultStmt>(default_0));
      }
    }
  }
}

TEST_SUITE("ASTBuilder::CreateSwitchStmt") {
  SCENARIO("Create a switch statement") {
    GIVEN("Global variable int x") {
      auto unit{GetASTUnit("int x;")};
      auto &ctx{unit->getASTContext()};
      rellic::ASTBuilder ast(*unit);
      auto tudecl{ctx.getTranslationUnitDecl()};
      THEN("return a switch(x)") {
        auto ref_a{GetDeclRef<clang::VarDecl>(ast, tudecl, "x")};
        auto switch_x{ast.CreateSwitchStmt(ref_a)};
        REQUIRE(switch_x != nullptr);
        CHECK(clang::isa<clang::SwitchStmt>(switch_x));
      }
    }
  }
}