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
  } else if (value_size > ctx.getIntWidth(ctx.LongLongTy)) {
    type = sign ? ctx.LongLongTy : ctx.UnsignedLongLongTy;
  } else {
    type = GetLeastIntTypeForBitWidth(ctx, value_size, sign);
  }
  // Extend the literal value based on it's sign if we have a
  // mismatch between the bit width of the value and inferred type.
  auto type_size{ctx.getIntWidth(type)};
  if (val.getBitWidth() != type_size && val.getMinSignedBits() < type_size) {
    val = val.extOrTrunc(type_size);
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
  if (value_size <= ctx.getIntWidth(ctx.ShortTy) ||
      value_size > ctx.getIntWidth(ctx.LongLongTy)) {
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
  return clang::VarDecl::Create(
      ctx, decl_ctx, clang::SourceLocation(), clang::SourceLocation(), id, type,
      ctx.getTrivialTypeSourceInfo(type), clang::SC_None);
}

clang::FunctionDecl *ASTBuilder::CreateFunctionDecl(
    clang::DeclContext *decl_ctx, clang::QualType type,
    clang::IdentifierInfo *id) {
  return clang::FunctionDecl::Create(
      ctx, decl_ctx, clang::SourceLocation(), clang::SourceLocation(),
      clang::DeclarationName(id), type, ctx.getTrivialTypeSourceInfo(type),
      clang::SC_None, /*isInlineSpecified=*/false);
}

clang::RecordDecl *ASTBuilder::CreateStructDecl(clang::DeclContext *decl_ctx,
                                                clang::IdentifierInfo *id,
                                                clang::RecordDecl *prev_decl) {
  return clang::RecordDecl::Create(ctx, clang::TagTypeKind::TTK_Struct,
                                   decl_ctx, clang::SourceLocation(),
                                   clang::SourceLocation(), id, prev_decl);
}

clang::FieldDecl *ASTBuilder::CreateFieldDecl(clang::RecordDecl *record,
                                              clang::QualType type,
                                              clang::IdentifierInfo *id) {
  return sema.CheckFieldDecl(
      clang::DeclarationName(id), type, ctx.getTrivialTypeSourceInfo(type),
      record, clang::SourceLocation(), /*Mutable=*/false, /*BitWidth=*/nullptr,
      clang::ICIS_NoInit, clang::SourceLocation(),
      clang::AccessSpecifier::AS_none,
      /*PrevDecl=*/nullptr);
}

clang::DeclStmt *ASTBuilder::CreateDeclStmt(clang::Decl *decl) {
  return new (ctx)
      clang::DeclStmt(clang::DeclGroupRef(decl), clang::SourceLocation(),
                      clang::SourceLocation());
}

clang::DeclRefExpr *ASTBuilder::CreateDeclRef(clang::ValueDecl *val) {
  CHECK(val) << "Should not be null in CreateDeclRef.";
  clang::DeclarationNameInfo dni(val->getDeclName(), clang::SourceLocation());
  clang::CXXScopeSpec ss;
  auto er{sema.BuildDeclarationNameExpr(ss, dni, val)};
  CHECK(er.isUsable());
  return er.getAs<clang::DeclRefExpr>();
}

clang::ParenExpr *ASTBuilder::CreateParen(clang::Expr *expr) {
  return new (ctx)
      clang::ParenExpr(clang::SourceLocation(), clang::SourceLocation(), expr);
}

clang::CStyleCastExpr *ASTBuilder::CreateCStyleCast(clang::QualType type,
                                                    clang::Expr *expr) {
  auto er{sema.BuildCStyleCastExpr(clang::SourceLocation(),
                                   ctx.getTrivialTypeSourceInfo(type),
                                   clang::SourceLocation(), expr)};
  CHECK(er.isUsable());
  return er.getAs<clang::CStyleCastExpr>();
}

clang::UnaryOperator *ASTBuilder::CreateUnaryOp(clang::UnaryOperatorKind opc,
                                                clang::Expr *expr) {
  CHECK(expr) << "Should not be null in CreateUnaryOp.";
  auto er{sema.CreateBuiltinUnaryOp(clang::SourceLocation(), opc, expr)};
  CHECK(er.isUsable());
  return er.getAs<clang::UnaryOperator>();
}

clang::BinaryOperator *ASTBuilder::CreateBinaryOp(clang::BinaryOperatorKind opc,
                                                  clang::Expr *lhs,
                                                  clang::Expr *rhs) {
  CHECK(lhs && rhs) << "Should not be null in CreateBinaryOp.";
  auto er{sema.CreateBuiltinBinOp(clang::SourceLocation(), opc, lhs, rhs)};
  CHECK(er.isUsable());
  return er.getAs<clang::BinaryOperator>();
}

clang::ArraySubscriptExpr *ASTBuilder::CreateArraySub(clang::Expr *base,
                                                      clang::Expr *idx) {
  CHECK(base && idx) << "Should not be null in CreateBinaryOp.";
  auto er{sema.CreateBuiltinArraySubscriptExpr(base, clang::SourceLocation(),
                                               idx, clang::SourceLocation())};
  CHECK(er.isUsable());
  return er.getAs<clang::ArraySubscriptExpr>();
}

}  // namespace rellic