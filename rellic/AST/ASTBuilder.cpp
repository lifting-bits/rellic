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
#include "rellic/AST/Util.h"

namespace rellic {

ASTBuilder::ASTBuilder(clang::ASTContext &context) : ctx(context) {}

clang::IntegerLiteral *ASTBuilder::CreateIntLit(llvm::APSInt val) {
  auto sign{val.isSigned()};
  auto value_size{val.getBitWidth()};
  // Infer integer type wide enough to accommodate the value,
  // with `unsigned int` being the smallest type allowed.
  clang::QualType type;
  if (value_size <= ctx.getIntWidth(ctx.IntTy)) {
    type = sign ? ctx.IntTy : ctx.UnsignedIntTy;
  } else {
    type = GetLeastIntTypeForBitWidth(ctx, value_size, sign);
  }
  // Extend the literal value based on it's sign if we have a
  // mismatch between the bit width of the value and inferred type.
  auto type_size{ctx.getIntWidth(type)};
  if (value_size < type_size) {
    val = sign ? val.sextOrSelf(type_size) : val.zextOrSelf(type_size);
  }
  // Clang does this check in the `clang::IntegerLiteral::Create`, but
  // we've had the calls with mismatched bit widths succeed before so
  // just in case we have ours here too.
  CHECK_EQ(val.getBitWidth(), ctx.getIntWidth(type));
  return clang::IntegerLiteral::Create(ctx, val, type, clang::SourceLocation());
}

clang::Expr *ASTBuilder::CreateAdjustedIntLit(llvm::APSInt val) {
  auto lit{CreateIntLit(val)};
  auto value_size{val.getBitWidth()};
  // Cast the integer literal to a type of the smallest bit width
  // that can contain `val`. Either `short` or `char`.
  if (value_size <= ctx.getIntWidth(ctx.ShortTy)) {
    return CreateCStyleCastExpr(
        ctx, GetLeastIntTypeForBitWidth(ctx, value_size, val.isSigned()),
        clang::CastKind::CK_IntegralCast, lit);
  } else {
    return lit;
  }
}

clang::CharacterLiteral *ASTBuilder::CreateCharLit(llvm::APInt val) {
  CHECK(val.getBitWidth() == 8U);
  return new (ctx) clang::CharacterLiteral(
      val.getLimitedValue(), clang::CharacterLiteral::CharacterKind::Ascii,
      ctx.IntTy, clang::SourceLocation());
}

clang::StringLiteral *ASTBuilder::CreateStrLit(std::string val) {
  auto type{ctx.getStringLiteralArrayType(ctx.CharTy, val.size())};
  return clang::StringLiteral::Create(
      ctx, val, clang::StringLiteral::StringKind::Ascii,
      /*Pascal=*/false, type, clang::SourceLocation());
}

clang::FloatingLiteral *ASTBuilder::CreateFPLit(llvm::APFloat val) {
  auto size{llvm::APFloat::getSizeInBits(val.getSemantics())};
  auto type{GetRealTypeForBitwidth(ctx, size)};
  CHECK(!type.isNull()) << "Unable to infer type for given value.";
  return clang::FloatingLiteral::Create(ctx, val, /*isexact=*/true, type,
                                        clang::SourceLocation());
}

}  // namespace rellic