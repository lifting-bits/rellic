/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/ASTBuilder.h"

#include <clang/Sema/Sema.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/Compat/ASTContext.h"
#include "rellic/AST/Util.h"

namespace rellic {

ASTBuilder::ASTBuilder(clang::ASTUnit &unit)
    : unit(unit), ctx(unit.getASTContext()), sema(unit.getSema()) {}

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
    return CreateCStyleCast(
        GetLeastIntTypeForBitWidth(ctx, value_size, val.isSigned()), lit);
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

clang::Expr *ASTBuilder::CreateNull() {
  auto type{ctx.UnsignedIntTy};
  auto val{llvm::APInt::getNullValue(ctx.getTypeSize(type))};
  auto lit{CreateIntLit(val)};
  return CreateCStyleCast(ctx.VoidPtrTy, lit);
};

clang::Expr *ASTBuilder::CreateUndef(clang::QualType type) {
  auto null{CreateNull()};
  auto cast{CreateCStyleCast(ctx.getPointerType(type), null)};
  return CreateDeref(cast);
};

clang::IdentifierInfo *ASTBuilder::CreateIdentifier(std::string name) {
  std::string str{""};
  for (auto chr : name) {
    str.push_back(std::isalnum(chr) ? chr : '_');
  }
  return &ctx.Idents.get(str);
}

clang::VarDecl *ASTBuilder::CreateVarDecl(clang::DeclContext *decl_ctx,
                                          clang::QualType type,
                                          clang::IdentifierInfo *id) {
  auto var{clang::VarDecl::Create(ctx, decl_ctx, clang::SourceLocation(),
                                  clang::SourceLocation(), id, type,
                                  /*TypeSourceInfo=*/nullptr, clang::SC_None)};
  decl_ctx->addDecl(var);
  return var;
}

clang::DeclStmt *ASTBuilder::CreateDeclStmt(clang::Decl *decl) {
  return new (ctx)
      clang::DeclStmt(clang::DeclGroupRef(decl), clang::SourceLocation(),
                      clang::SourceLocation());
}

clang::DeclRefExpr *ASTBuilder::CreateDeclRef(clang::ValueDecl *val) {
  CHECK(val) << "Should not be null in CreateDeclRef.";
  return clang::DeclRefExpr::Create(
      ctx, clang::NestedNameSpecifierLoc(), clang::SourceLocation(), val, false,
      val->getLocation(), val->getType(), clang::VK_LValue);
}

clang::CStyleCastExpr *ASTBuilder::CreateCStyleCast(clang::QualType type,
                                                    clang::Expr *expr) {
  clang::ActionResult<clang::Expr *> ar(expr);
  auto kind{sema.PrepareScalarCast(ar, type)};
  return clang::CStyleCastExpr::Create(
      ctx, type, clang::VK_RValue, kind, expr, nullptr,
      ctx.getTrivialTypeSourceInfo(type), clang::SourceLocation(),
      clang::SourceLocation());
}

clang::ImplicitCastExpr *ASTBuilder::CreateImplicitCast(clang::QualType type,
                                                        clang::Expr *expr) {
  return nullptr;
}

clang::UnaryOperator *ASTBuilder::CreateDeref(clang::Expr *expr) {
  auto type{expr->getType()};
  CHECK(type->isPointerType());
  return CreateUnaryOperator(ctx, clang::UO_Deref, expr,
                             type->getPointeeType());
}

clang::UnaryOperator *ASTBuilder::CreateAddrOf(clang::Expr *expr) {
  auto type{ctx.getPointerType(expr->getType())};
  return CreateUnaryOperator(ctx, clang::UO_AddrOf, expr, type);
}

clang::UnaryOperator *ASTBuilder::CreateAddrOf(clang::ValueDecl *decl) {
  CHECK(clang::isa<clang::FunctionDecl>(decl) ||
        clang::isa<clang::VarDecl>(decl));
  return CreateAddrOf(CreateDeclRef(decl));
}

clang::UnaryOperator *ASTBuilder::CreateLNot(clang::Expr *expr) {
  return nullptr;
}

clang::UnaryOperator *ASTBuilder::CreateNot(clang::Expr *expr) {
  return nullptr;
}

}  // namespace rellic