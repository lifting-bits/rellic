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
  clang::Expr *CreateIntLit(llvm::APInt val);
  clang::CharacterLiteral *CreateCharLit(llvm::APInt val);
  clang::StringLiteral *CreateStrLit(std::string val);
  clang::FloatingLiteral *CreateFPLit(llvm::APFloat val);
};

}  // namespace rellic