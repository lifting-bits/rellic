/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <string>

#include "rellic/AST/Compat/Expr.h"

namespace rellic {

class ASTBuilder {
 private:
  clang::ASTContext &ctx;

 public:
  ASTBuilder(clang::ASTContext &context);
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
  // C-style casting
  clang::CStyleCastExpr *CreateCStyleCast(clang::QualType type,
                                          clang::Expr *expr);
};

}  // namespace rellic