/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/Frontend/ASTUnit.h>

#include <string>

namespace clang {
class Sema;
}  // namespace clang

namespace rellic {

class ASTBuilder {
 private:
  clang::ASTUnit &unit;
  clang::ASTContext &ctx;
  clang::Sema &sema;

 public:
  ASTBuilder(clang::ASTUnit &unit);
  // Literals
  clang::IntegerLiteral *CreateIntLit(llvm::APSInt val);

  clang::IntegerLiteral *CreateIntLit(llvm::APInt val) {
    return CreateIntLit(llvm::APSInt(val, /*isUnsigned=*/true));
  };

  clang::IntegerLiteral *CreateTrue() {
    return CreateIntLit(llvm::APInt(/*numBits=*/1U, /*val*/ 1U));
  };

  clang::IntegerLiteral *CreateFalse() {
    return CreateIntLit(llvm::APInt(/*numBits=*/1U, /*val*/ 0U));
  };

  clang::CharacterLiteral *CreateCharLit(llvm::APInt val);
  clang::StringLiteral *CreateStrLit(std::string val);
  clang::FloatingLiteral *CreateFPLit(llvm::APFloat val);
  // Casted literals
  clang::Expr *CreateAdjustedIntLit(llvm::APSInt val);

  clang::Expr *CreateAdjustedIntLit(llvm::APInt val) {
    return CreateAdjustedIntLit(llvm::APSInt(val, /*isUnsigned=*/true));
  };
  // Special values
  clang::Expr *CreateNull();
  clang::Expr *CreateUndef(clang::QualType type);
  // Identifiers
  clang::IdentifierInfo *CreateIdentifier(std::string name);
  // Variable declaration
  clang::VarDecl *CreateVarDecl(clang::DeclContext *decl_ctx,
                                clang::QualType type,
                                clang::IdentifierInfo *id);

  clang::VarDecl *CreateVarDecl(clang::DeclContext *ctx, clang::QualType type,
                                std::string name) {
    return CreateVarDecl(ctx, type, CreateIdentifier(name));
  }

  clang::DeclStmt *CreateDeclStmt(clang::Decl *decl);
  clang::DeclRefExpr *CreateDeclRef(clang::ValueDecl *val);

  clang::ParenExpr *CreateParen(clang::Expr *expr);

  // C-style casting
  clang::CStyleCastExpr *CreateCStyleCast(clang::QualType type,
                                          clang::Expr *expr);
  // Implicit casting
  clang::ImplicitCastExpr *CreateImplicitCast(clang::QualType type,
                                              clang::Expr *expr);
  // Unary operators
  clang::UnaryOperator *CreateUnaryOp(clang::UnaryOperatorKind opc,
                                      clang::Expr *expr);

  clang::UnaryOperator *CreateDeref(clang::Expr *expr) {
    return CreateUnaryOp(clang::UO_Deref, expr);
  }

  clang::UnaryOperator *CreateAddrOf(clang::Expr *expr) {
    return CreateUnaryOp(clang::UO_AddrOf, expr);
  }

  clang::UnaryOperator *CreateLNot(clang::Expr *expr) {
    return CreateUnaryOp(clang::UO_LNot, expr);
  }

  clang::UnaryOperator *CreateNot(clang::Expr *expr) {
    return CreateUnaryOp(clang::UO_Not, expr);
  }
};

}  // namespace rellic