/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/ASTBuilder.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Sema/Lookup.h>
#include <clang/Sema/Sema.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/Util.h"
#include "rellic/Exception.h"

namespace rellic {

enum CExprPrecedence : unsigned {
  Value = 0U,
  SpecialOp,
  UnaryOp,
  BinaryOp = UnaryOp + clang::UO_LNot + 1U,
  CondOp = BinaryOp + clang::BO_Comma + 1U
};

namespace {

static unsigned GetOperatorPrecedence(clang::UnaryOperatorKind opc) {
  return static_cast<unsigned>(CExprPrecedence::UnaryOp) +
         static_cast<unsigned>(opc);
}

static unsigned GetOperatorPrecedence(clang::BinaryOperatorKind opc) {
  return static_cast<unsigned>(CExprPrecedence::BinaryOp) +
         static_cast<unsigned>(opc);
}

static unsigned GetOperatorPrecedence(clang::Expr *op) {
  if (auto cast = clang::dyn_cast<clang::ImplicitCastExpr>(op)) {
    return GetOperatorPrecedence(cast->getSubExpr());
  }

  if (clang::isa<clang::DeclRefExpr>(op) ||
      clang::isa<clang::IntegerLiteral>(op) ||
      clang::isa<clang::FloatingLiteral>(op) ||
      clang::isa<clang::InitListExpr>(op) ||
      clang::isa<clang::CompoundLiteralExpr>(op) ||
      clang::isa<clang::ParenExpr>(op) ||
      clang::isa<clang::StringLiteral>(op)) {
    return CExprPrecedence::Value;
  }

  if (clang::isa<clang::MemberExpr>(op) ||
      clang::isa<clang::ArraySubscriptExpr>(op) ||
      clang::isa<clang::CallExpr>(op)) {
    return CExprPrecedence::SpecialOp;
  }

  if (clang::isa<clang::CStyleCastExpr>(op)) {
    return CExprPrecedence::UnaryOp;
  }

  if (auto uo = clang::dyn_cast<clang::UnaryOperator>(op)) {
    return GetOperatorPrecedence(uo->getOpcode());
  }

  if (auto bo = clang::dyn_cast<clang::BinaryOperator>(op)) {
    return GetOperatorPrecedence(bo->getOpcode());
  }

  if (clang::isa<clang::ConditionalOperator>(op)) {
    return CExprPrecedence::CondOp;
  }

  THROW() << "Unknown clang::Expr " << ClangThingToString(op);

  return 0U;
}

}  // namespace

ASTBuilder::ASTBuilder(clang::ASTUnit &unit)
    : unit(unit), ctx(unit.getASTContext()), sema(unit.getSema()) {}

clang::QualType ASTBuilder::GetLeastIntTypeForBitWidth(unsigned size,
                                                       unsigned sign) {
  auto result{ctx.getIntTypeForBitwidth(size, sign)};
  if (!result.isNull()) {
    return result;
  }
  auto &ti{ctx.getTargetInfo()};
  auto target_type{ti.getLeastIntTypeByWidth(size, sign)};

  CHECK_THROW(target_type != clang::TargetInfo::IntType::NoInt)
      << "Failed to infer clang::TargetInfo::IntType for bitwidth: " << size;

  result = ctx.getIntTypeForBitwidth(ti.getTypeWidth(target_type), sign);

  CHECK_THROW(!result.isNull())
      << "Failed to infer clang::QualType for bitwidth: " << size;

  return result;
}

clang::QualType ASTBuilder::GetLeastRealTypeForBitWidth(unsigned size) {
  auto result{ctx.getRealTypeForBitwidth(size, clang::FloatModeKind::Float)};
  if (!result.isNull()) {
    return result;
  }

  if (size <= ctx.getTypeSize(ctx.FloatTy)) {
    return ctx.FloatTy;
  }

  if (size <= ctx.getTypeSize(ctx.DoubleTy)) {
    return ctx.DoubleTy;
  }

  if (size <= ctx.getTypeSize(ctx.LongDoubleTy)) {
    return ctx.LongDoubleTy;
  }

  THROW() << "Failed to infer real clang::QualType for bitwidth: " << size;

  return clang::QualType();
}

clang::Expr *ASTBuilder::CreateIntLit(llvm::APSInt val) {
  auto sign{val.isSigned()};
  auto value_size{val.getBitWidth()};
  // Infer integer type wide enough to accommodate the value,
  // with `unsigned int` being the smallest type allowed.
  auto llsize{ctx.getIntWidth(ctx.LongLongTy)};
  clang::QualType type{GetLeastIntTypeForBitWidth(value_size, sign)};

  // C doesn't have a literal suffix for integers wider than a long long.
  // If we encounter such a case, try to either
  //  a) truncate it if the number of significant bits allows it, then cast to
  //    an appropriate size, or
  //  b) split it into chunks, then cast shift and merge
  //    into a value.
  if (val.getSignificantBits() > llsize) {
    clang::Expr *res{};
    for (size_t i = 0; i < value_size / llsize; ++i) {
      auto part{CreateCStyleCast(type, CreateIntLit(val.extOrTrunc(llsize)))};
      val = val >> llsize;
      if (res) {
        res = CreateOr(
            res,
            CreateShl(part, CreateIntLit(llvm::APInt(32, i * llsize, true))));
      } else {
        res = part;
      }
    }
    return CHECK_NOTNULL(res);
  } else if (value_size > llsize) {
    val = val.trunc(llsize);
    auto lit = CreateIntLit(val);
    return CreateCStyleCast(type, lit);
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
  CHECK_EQ(val.getBitWidth(), ctx.getIntWidth(type))
      << "Produced type does not match wanted width: "
      << ClangThingToString(type);
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
        GetLeastIntTypeForBitWidth(value_size, val.isSigned()), lit);
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

clang::CharacterLiteral *ASTBuilder::CreateCharLit(unsigned val) {
  return new (ctx) clang::CharacterLiteral(
      val, clang::CharacterLiteral::CharacterKind::Ascii, ctx.IntTy,
      clang::SourceLocation());
}

clang::StringLiteral *ASTBuilder::CreateStrLit(std::string val) {
  auto type{ctx.getStringLiteralArrayType(ctx.CharTy, val.size())};
  return clang::StringLiteral::Create(
      ctx, val, clang::StringLiteral::StringKind::Ordinary,
      /*Pascal=*/false, type, clang::SourceLocation());
}

clang::Expr *ASTBuilder::CreateFPLit(llvm::APFloat val) {
  auto size{llvm::APFloat::getSizeInBits(val.getSemantics())};
  auto type{GetLeastRealTypeForBitWidth(size)};
  CHECK_THROW(!type.isNull()) << "Unable to infer type for given value.";
  if (val.isNaN()) {
    std::vector<clang::Expr *> args{CreateStrLit("")};
    return CreateBuiltinCall(clang::Builtin::BI__builtin_nan, args);
  } else if (val.isInfinity()) {
    std::vector<clang::Expr *> args;
    auto inf{CreateBuiltinCall(clang::Builtin::BI__builtin_inf, args)};
    if (val.isNegative()) {
      return CreateUnaryOp(clang::UO_Minus, inf);
    } else {
      return inf;
    }
  }
  return clang::FloatingLiteral::Create(ctx, val, /*isexact=*/true, type,
                                        clang::SourceLocation());
}

clang::Expr *ASTBuilder::CreateNull() {
  auto type{ctx.UnsignedIntTy};
  auto val{llvm::APInt::getNullValue(ctx.getTypeSize(type))};
  auto lit{CreateIntLit(val)};
  return CreateCStyleCast(ctx.VoidPtrTy, lit);
}

clang::Expr *ASTBuilder::CreateUndefInteger(clang::QualType type) {
  auto val{llvm::APInt::getNullValue(ctx.getTypeSize(type))};
  auto lit{CreateIntLit(val)};
  return lit;
}

clang::Expr *ASTBuilder::CreateUndefPointer(clang::QualType type) {
  auto null{CreateNull()};
  auto cast{CreateCStyleCast(ctx.getPointerType(type), null)};
  return cast;
}

clang::IdentifierInfo *ASTBuilder::CreateIdentifier(std::string name) {
  std::string str{""};
  for (auto chr : name) {
    str.push_back(std::isalnum(chr) ? chr : '_');
  }
  return &ctx.Idents.get(str);
}

clang::VarDecl *ASTBuilder::CreateVarDecl(clang::DeclContext *decl_ctx,
                                          clang::QualType type,
                                          clang::IdentifierInfo *id,
                                          clang::StorageClass storage_class) {
  return clang::VarDecl::Create(
      ctx, decl_ctx, clang::SourceLocation(), clang::SourceLocation(), id, type,
      ctx.getTrivialTypeSourceInfo(type), storage_class);
}

clang::FunctionDecl *ASTBuilder::CreateFunctionDecl(
    clang::DeclContext *decl_ctx, clang::QualType type,
    clang::IdentifierInfo *id) {
  return clang::FunctionDecl::Create(
      ctx, decl_ctx, clang::SourceLocation(), clang::SourceLocation(),
      clang::DeclarationName(id), type, ctx.getTrivialTypeSourceInfo(type),
      clang::SC_None, /*isInlineSpecified=*/false);
}

clang::ParmVarDecl *ASTBuilder::CreateParamDecl(clang::DeclContext *decl_ctx,
                                                clang::QualType type,
                                                clang::IdentifierInfo *id) {
  return sema.CheckParameter(
      decl_ctx, clang::SourceLocation(), clang::SourceLocation(), id, type,
      ctx.getTrivialTypeSourceInfo(type), clang::SC_None);
}

clang::RecordDecl *ASTBuilder::CreateStructDecl(clang::DeclContext *decl_ctx,
                                                clang::IdentifierInfo *id,
                                                clang::RecordDecl *prev_decl) {
  return clang::RecordDecl::Create(ctx, clang::TagTypeKind::TTK_Struct,
                                   decl_ctx, clang::SourceLocation(),
                                   clang::SourceLocation(), id, prev_decl);
}

clang::RecordDecl *ASTBuilder::CreateUnionDecl(clang::DeclContext *decl_ctx,
                                               clang::IdentifierInfo *id,
                                               clang::RecordDecl *prev_decl) {
  return clang::RecordDecl::Create(ctx, clang::TagTypeKind::TTK_Union, decl_ctx,
                                   clang::SourceLocation(),
                                   clang::SourceLocation(), id, prev_decl);
}

clang::EnumDecl *ASTBuilder::CreateEnumDecl(clang::DeclContext *decl_ctx,
                                            clang::IdentifierInfo *id,
                                            clang::EnumDecl *prev_decl) {
  return clang::EnumDecl::Create(ctx, decl_ctx, clang::SourceLocation(),
                                 clang::SourceLocation(), id, prev_decl, false,
                                 false, false);
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

clang::FieldDecl *ASTBuilder::CreateFieldDecl(clang::RecordDecl *record,
                                              clang::QualType type,
                                              clang::IdentifierInfo *id,
                                              unsigned bitwidth) {
  auto bw{clang::IntegerLiteral::Create(ctx, llvm::APInt(32, bitwidth),
                                        ctx.IntTy, clang::SourceLocation())};
  return sema.CheckFieldDecl(clang::DeclarationName(id), type,
                             ctx.getTrivialTypeSourceInfo(type), record,
                             clang::SourceLocation(), /*Mutable=*/false, bw,
                             clang::ICIS_NoInit, clang::SourceLocation(),
                             clang::AccessSpecifier::AS_none,
                             /*PrevDecl=*/nullptr);
}

clang::EnumConstantDecl *ASTBuilder::CreateEnumConstantDecl(
    clang::EnumDecl *e, clang::IdentifierInfo *id, clang::Expr *expr,
    clang::EnumConstantDecl *previousConstant) {
  return sema.CheckEnumConstant(e, previousConstant, clang::SourceLocation(),
                                id, expr);
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
  CHECK(expr) << "Should not be null in CreateCStyleCast.";
  if (CExprPrecedence::UnaryOp < GetOperatorPrecedence(expr)) {
    expr = CreateParen(expr);
  }
  auto er{sema.BuildCStyleCastExpr(clang::SourceLocation(),
                                   ctx.getTrivialTypeSourceInfo(type),
                                   clang::SourceLocation(), expr)};
  CHECK(er.isUsable());
  return er.getAs<clang::CStyleCastExpr>();
}

clang::UnaryOperator *ASTBuilder::CreateUnaryOp(clang::UnaryOperatorKind opc,
                                                clang::Expr *expr) {
  CHECK(expr) << "Should not be null in CreateUnaryOp.";
  if (GetOperatorPrecedence(opc) < GetOperatorPrecedence(expr)) {
    expr = CreateParen(expr);
  }
  auto er{sema.CreateBuiltinUnaryOp(clang::SourceLocation(), opc, expr)};
  CHECK(er.isUsable());
  return er.getAs<clang::UnaryOperator>();
}

clang::BinaryOperator *ASTBuilder::CreateBinaryOp(clang::BinaryOperatorKind opc,
                                                  clang::Expr *lhs,
                                                  clang::Expr *rhs) {
  CHECK(lhs && rhs) << "Should not be null in CreateBinaryOp.";
  if (GetOperatorPrecedence(opc) < GetOperatorPrecedence(lhs)) {
    lhs = CreateParen(lhs);
  }
  if (GetOperatorPrecedence(opc) < GetOperatorPrecedence(rhs)) {
    rhs = CreateParen(rhs);
  }
  auto er{sema.CreateBuiltinBinOp(clang::SourceLocation(), opc, lhs, rhs)};
  CHECK(er.isUsable());
  return er.getAs<clang::BinaryOperator>();
}

clang::ConditionalOperator *ASTBuilder::CreateConditional(clang::Expr *cond,
                                                          clang::Expr *lhs,
                                                          clang::Expr *rhs) {
  auto er{sema.ActOnConditionalOp(clang::SourceLocation(),
                                  clang::SourceLocation(), cond, lhs, rhs)};
  CHECK(er.isUsable());
  return er.getAs<clang::ConditionalOperator>();
}

clang::ArraySubscriptExpr *ASTBuilder::CreateArraySub(clang::Expr *base,
                                                      clang::Expr *idx) {
  CHECK(base && idx) << "Should not be null in CreateArraySub.";
  if (CExprPrecedence::SpecialOp < GetOperatorPrecedence(base)) {
    base = CreateParen(base);
  }
  auto er{sema.CreateBuiltinArraySubscriptExpr(base, clang::SourceLocation(),
                                               idx, clang::SourceLocation())};
  CHECK(er.isUsable());
  return er.getAs<clang::ArraySubscriptExpr>();
}

clang::CallExpr *ASTBuilder::CreateCall(clang::Expr *callee,
                                        std::vector<clang::Expr *> &args) {
  CHECK(callee) << "Should not be null in CreateCall.";
  if (CExprPrecedence::SpecialOp < GetOperatorPrecedence(callee)) {
    callee = CreateParen(callee);
  }
  auto er{sema.BuildCallExpr(/*Scope=*/nullptr, callee, clang::SourceLocation(),
                             args, clang::SourceLocation())};
  CHECK(er.isUsable());
  return er.getAs<clang::CallExpr>();
}

clang::Expr *ASTBuilder::CreateBuiltinCall(clang::Builtin::ID builtin,
                                           std::vector<clang::Expr *> &args) {
  auto name{ctx.BuiltinInfo.getName(builtin)};
  clang::SourceLocation loc;
  clang::LookupResult R(sema, &ctx.Idents.get(name), loc,
                        clang::Sema::LookupOrdinaryName);
  clang::Sema::LookupNameKind NameKind = R.getLookupKind();
  auto II{R.getLookupName().getAsIdentifierInfo()};
  clang::ASTContext::GetBuiltinTypeError error;
  auto ty{ctx.GetBuiltinType(builtin, error)};
  CHECK(!error);
  auto decl{sema.CreateBuiltin(II, ty, builtin, loc)};

  return CreateCall(decl, args);
}

clang::MemberExpr *ASTBuilder::CreateFieldAcc(clang::Expr *base,
                                              clang::FieldDecl *field,
                                              bool is_arrow) {
  CHECK(base && field) << "Should not be null in CreateFieldAcc.";
  CHECK(!is_arrow || base->getType()->isPointerType())
      << "Base operand in arrow operator must be a pointer!";
  clang::CXXScopeSpec ss;
  auto dap{clang::DeclAccessPair::make(field, field->getAccess())};
  auto er{sema.BuildFieldReferenceExpr(base, is_arrow, clang::SourceLocation(),
                                       ss, field, dap,
                                       clang::DeclarationNameInfo())};
  CHECK(er.isUsable());
  return er.getAs<clang::MemberExpr>();
}

clang::InitListExpr *ASTBuilder::CreateInitList(
    std::vector<clang::Expr *> &exprs) {
  auto er{sema.ActOnInitList(clang::SourceLocation(), exprs,
                             clang::SourceLocation())};
  CHECK(er.isUsable());
  return er.getAs<clang::InitListExpr>();
}

clang::CompoundStmt *ASTBuilder::CreateCompoundStmt(
    std::vector<clang::Stmt *> &stmts) {
  // sema.ActOnStartOfCompoundStmt(/*isStmtExpr=*/false);
  // auto sr{sema.ActOnCompoundStmt(clang::SourceLocation(),
  //                                clang::SourceLocation(), stmts,
  //                                /*isStmtExpr=*/false)};
  // sema.ActOnFinishOfCompoundStmt();
  // CHECK(sr.isUsable());
  // return sr.getAs<clang::CompoundStmt>();
  return clang::CompoundStmt::Create(ctx, stmts, clang::FPOptionsOverride{},
                                     clang::SourceLocation(),
                                     clang::SourceLocation());
}

clang::CompoundLiteralExpr *ASTBuilder::CreateCompoundLit(clang::QualType type,
                                                          clang::Expr *expr) {
  auto er{sema.BuildCompoundLiteralExpr(clang::SourceLocation(),
                                        ctx.getTrivialTypeSourceInfo(type),
                                        clang::SourceLocation(), expr)};
  CHECK(er.isUsable());
  return er.getAs<clang::CompoundLiteralExpr>();
}

clang::IfStmt *ASTBuilder::CreateIf(clang::Expr *cond, clang::Stmt *then_val,
                                    clang::Stmt *else_val) {
  CHECK(cond && then_val) << "Should not be null in CreateIf.";
  auto cr{sema.ActOnCondition(/*Scope=*/nullptr, clang::SourceLocation(), cond,
                              clang::Sema::ConditionKind::Boolean)};
  CHECK(!cr.isInvalid());
  auto if_stmt{clang::IfStmt::CreateEmpty(ctx, true, false, false)};
  if_stmt->setCond(cr.get().second);
  if_stmt->setThen(then_val);
  if_stmt->setElse(else_val);
  if_stmt->setStatementKind(clang::IfStatementKind::Ordinary);
  return if_stmt;
}

clang::WhileStmt *ASTBuilder::CreateWhile(clang::Expr *cond,
                                          clang::Stmt *body) {
  // auto sr{sema.ActOnWhileStmt(clang::SourceLocation(),
  // clang::SourceLocation(),
  //                             cond, clang::SourceLocation(), body)};
  // CHECK(sr.isUsable());
  // return sr.getAs<clang::WhileStmt>();
  CHECK(cond != nullptr) << "Should not be null in CreateWhile.";
  auto cer{sema.CheckBooleanCondition(clang::SourceLocation(), cond)};
  CHECK(!cer.isInvalid());
  return clang::WhileStmt::Create(
      ctx, nullptr, cond, body, clang::SourceLocation(),
      clang::SourceLocation(), clang::SourceLocation());
}

clang::DoStmt *ASTBuilder::CreateDo(clang::Expr *cond, clang::Stmt *body) {
  // auto sr{sema.ActOnDoStmt(clang::SourceLocation(), body,
  //                          clang::SourceLocation(),
  //                          clang::SourceLocation(), cond,
  //                          clang::SourceLocation())};
  // CHECK(sr.isUsable());
  // return sr.getAs<clang::DoStmt>();
  CHECK(cond != nullptr) << "Should not be null in CreateDo.";
  auto cer{sema.CheckBooleanCondition(clang::SourceLocation(), cond)};
  CHECK(!cer.isInvalid());
  cer = sema.ActOnFinishFullExpr(cer.get(), clang::SourceLocation(),
                                 /*DiscardedValue=*/false);
  CHECK(!cer.isInvalid());
  return new (ctx)
      clang::DoStmt(body, cond, clang::SourceLocation(),
                    clang::SourceLocation(), clang::SourceLocation());
}

clang::BreakStmt *ASTBuilder::CreateBreak() {
  return new (ctx) clang::BreakStmt(clang::SourceLocation());
}

clang::ReturnStmt *ASTBuilder::CreateReturn(clang::Expr *retval) {
  // auto sr{sema.BuildReturnStmt(clang::SourceLocation(), retval)};
  // CHECK(sr.isUsable());
  // return sr.getAs<clang::ReturnStmt>();
  return clang::ReturnStmt::Create(ctx, clang::SourceLocation(), retval,
                                   nullptr);
}

clang::TypedefDecl *ASTBuilder::CreateTypedefDecl(clang::DeclContext *decl_ctx,
                                                  clang::IdentifierInfo *id,
                                                  clang::QualType type) {
  return clang::TypedefDecl::Create(ctx, decl_ctx, clang::SourceLocation(),
                                    clang::SourceLocation(), id,
                                    ctx.getTrivialTypeSourceInfo(type));
}

clang::NullStmt *ASTBuilder::CreateNullStmt() {
  return new (ctx) clang::NullStmt(clang::SourceLocation());
}

clang::SwitchStmt *ASTBuilder::CreateSwitchStmt(clang::Expr *cond) {
  auto cc{sema.CheckSwitchCondition(clang::SourceLocation(), cond)};
  CHECK(!cc.isInvalid());
  return clang::SwitchStmt::Create(ctx, nullptr, nullptr, cc.get(),
                                   clang::SourceLocation(),
                                   clang::SourceLocation());
}

clang::CaseStmt *ASTBuilder::CreateCaseStmt(clang::Expr *cond) {
  return clang::CaseStmt::Create(ctx, cond, nullptr, clang::SourceLocation(),
                                 clang::SourceLocation(),
                                 clang::SourceLocation());
}

clang::DefaultStmt *ASTBuilder::CreateDefaultStmt(clang::Stmt *body) {
  return new (ctx) clang::DefaultStmt(clang::SourceLocation(),
                                      clang::SourceLocation(), body);
}

}  // namespace rellic
