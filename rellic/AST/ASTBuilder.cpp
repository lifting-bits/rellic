/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/ASTBuilder.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/Compat/ASTContext.h"

namespace rellic {

ASTBuilder::ASTBuilder(clang::ASTContext &context) : ctx(context) {}

clang::FloatingLiteral *ASTBuilder::CreateFPLit(llvm::APFloat val) {
  auto size{llvm::APFloat::getSizeInBits(val.getSemantics())};
  auto type{ctx.getRealTypeForBitwidth(size, /*ExplicitIEEE=*/true)};
  CHECK(!type.isNull()) << "Unable to infer type for given value.";
  return clang::FloatingLiteral::Create(ctx, val, /*isexact=*/true, type,
                                        clang::SourceLocation());
}

clang::Expr *ASTBuilder::CreateIntLit(llvm::APInt val) { return nullptr; }
clang::Expr *ASTBuilder::CreateCharLit(llvm::APInt val) { return nullptr; }
clang::Expr *ASTBuilder::CreateStrLit(std::string val) { return nullptr; }

}  // namespace rellic