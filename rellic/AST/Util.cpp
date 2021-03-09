/*
 * Copyright (c) 2018 Trail of Bits, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define GOOGLE_STRIP_LOG 1

#include "rellic/AST/Util.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/Compat/Expr.h"
#include "rellic/AST/Compat/Stmt.h"

namespace rellic {

namespace {

static clang::Expr *CreateBoolBinOp(clang::ASTContext &ctx,
                                    clang::BinaryOperatorKind opc,
                                    clang::Expr *lhs, clang::Expr *rhs) {
  CHECK(lhs || rhs) << "No operand given for binary logical expression";

  if (!lhs) {
    return rhs;
  } else if (!rhs) {
    return lhs;
  } else {
    return CreateBinaryOperator(ctx, opc, lhs, rhs, ctx.IntTy);
  }
}

}  // namespace

void InitCompilerInstance(clang::CompilerInstance &ins,
                          std::string target_triple) {
  ins.createDiagnostics();
  ins.getTargetOpts().Triple = target_triple;
  ins.setTarget(clang::TargetInfo::CreateTargetInfo(
      ins.getDiagnostics(), ins.getInvocation().TargetOpts));
  ins.createFileManager();
  ins.createSourceManager(ins.getFileManager());
  ins.createPreprocessor(clang::TU_Complete);
  ins.createASTContext();
}

bool ReplaceChildren(clang::Stmt *stmt, StmtMap &repl_map) {
  auto change = false;
  for (auto c_it = stmt->child_begin(); c_it != stmt->child_end(); ++c_it) {
    auto s_it = repl_map.find(*c_it);
    if (s_it != repl_map.end()) {
      *c_it = s_it->second;
      change = true;
    }
  }
  return change;
}

clang::QualType GetLeastIntTypeForBitWidth(clang::ASTContext &ctx,
                                           unsigned size, unsigned sign) {
  auto &ti{ctx.getTargetInfo()};
  auto target_type{ti.getLeastIntTypeByWidth(size, sign)};
  return ctx.getIntTypeForBitwidth(ti.getTypeWidth(target_type), sign);
}

clang::Expr *CastExpr(clang::ASTContext &ctx, clang::QualType dst,
                      clang::Expr *op) {
  // Get operand type
  auto src = op->getType();
  // Helpers
  auto IsInt = [&ctx](clang::QualType t) { return t->isIntegralType(ctx); };
  auto IsFloat = [](clang::QualType t) { return t->isFloatingType(); };
  auto IsSmaller = ctx.getTypeSize(src) < ctx.getTypeSize(dst);
  auto MakeCast = [&ctx, dst, op](clang::CastKind kind) {
    return CreateCStyleCastExpr(ctx, dst, kind, op);
  };

  // CK_FloatingCast
  if (IsFloat(dst) && IsFloat(src) && IsSmaller) {
    return MakeCast(clang::CastKind::CK_FloatingCast);
  }
  // CK_IntegralCast
  if (IsInt(dst) && IsInt(src) && IsSmaller) {
    return MakeCast(clang::CastKind::CK_IntegralCast);
  }
  // CK_IntegralToFloatingCast
  if (IsFloat(dst) && IsInt(src)) {
    return MakeCast(clang::CastKind::CK_IntegralToFloating);
  }
  // CK_FloatingToIntegralCast
  if (IsInt(dst) && IsFloat(src)) {
    return MakeCast(clang::CastKind::CK_FloatingToIntegral);
  }
  // Nothing
  return op;
}

clang::IdentifierInfo *CreateIdentifier(clang::ASTContext &ctx,
                                        std::string name) {
  std::string str = "";
  for (auto chr : name) {
    str.push_back(std::isalnum(chr) ? chr : '_');
  }
  return &ctx.Idents.get(str);
}

clang::DeclRefExpr *CreateDeclRefExpr(clang::ASTContext &ctx,
                                      clang::ValueDecl *val) {
  DLOG(INFO) << "Creating DeclRefExpr for " << val->getNameAsString();
  return clang::DeclRefExpr::Create(
      ctx, clang::NestedNameSpecifierLoc(), clang::SourceLocation(), val, false,
      val->getLocation(), val->getType(), clang::VK_LValue);
}

clang::DoStmt *CreateDoStmt(clang::ASTContext &ctx, clang::Expr *cond,
                            clang::Stmt *body) {
  return new (ctx)
      clang::DoStmt(body, cond, clang::SourceLocation(),
                    clang::SourceLocation(), clang::SourceLocation());
}

clang::BreakStmt *CreateBreakStmt(clang::ASTContext &ctx) {
  return new (ctx) clang::BreakStmt(clang::SourceLocation());
}

clang::ParenExpr *CreateParenExpr(clang::ASTContext &ctx, clang::Expr *expr) {
  return new (ctx)
      clang::ParenExpr(clang::SourceLocation(), clang::SourceLocation(), expr);
}

clang::Expr *CreateNotExpr(clang::ASTContext &ctx, clang::Expr *op) {
  CHECK(op) << "No operand given for unary logical expression";
  return CreateUnaryOperator(ctx, clang::UO_LNot, CreateParenExpr(ctx, op),
                             ctx.IntTy);
}

clang::Expr *CreateAndExpr(clang::ASTContext &ctx, clang::Expr *lhs,
                           clang::Expr *rhs) {
  return CreateBoolBinOp(ctx, clang::BO_LAnd, lhs, rhs);
}

clang::Expr *CreateOrExpr(clang::ASTContext &ctx, clang::Expr *lhs,
                          clang::Expr *rhs) {
  return CreateBoolBinOp(ctx, clang::BO_LOr, lhs, rhs);
}

clang::VarDecl *CreateVarDecl(clang::ASTContext &ctx,
                              clang::DeclContext *decl_ctx,
                              clang::IdentifierInfo *id, clang::QualType type) {
  return clang::VarDecl::Create(ctx, decl_ctx, clang::SourceLocation(),
                                clang::SourceLocation(), id, type, nullptr,
                                clang::SC_None);
}

clang::ParmVarDecl *CreateParmVarDecl(clang::ASTContext &ctx,
                                      clang::DeclContext *decl_ctx,
                                      clang::IdentifierInfo *id,
                                      clang::QualType type) {
  return clang::ParmVarDecl::Create(ctx, decl_ctx, clang::SourceLocation(),
                                    clang::SourceLocation(), id, type, nullptr,
                                    clang::SC_None, nullptr);
}

clang::FunctionDecl *CreateFunctionDecl(clang::ASTContext &ctx,
                                        clang::DeclContext *decl_ctx,
                                        clang::IdentifierInfo *id,
                                        clang::QualType type) {
  return clang::FunctionDecl::Create(
      ctx, decl_ctx, clang::SourceLocation(), clang::SourceLocation(),
      clang::DeclarationName(id), type, nullptr, clang::SC_None, false);
}

clang::FieldDecl *CreateFieldDecl(clang::ASTContext &ctx,
                                  clang::DeclContext *decl_ctx,
                                  clang::IdentifierInfo *id,
                                  clang::QualType type) {
  return clang::FieldDecl::Create(ctx, decl_ctx, clang::SourceLocation(),
                                  clang::SourceLocation(), id, type,
                                  /*TInfo=*/nullptr, /*BitWidth=*/nullptr,
                                  /*Mutable=*/false, clang::ICIS_NoInit);
}

clang::RecordDecl *CreateStructDecl(clang::ASTContext &ctx,
                                    clang::DeclContext *decl_ctx,
                                    clang::IdentifierInfo *id,
                                    clang::RecordDecl *prev_decl) {
  return clang::RecordDecl::Create(ctx, clang::TagTypeKind::TTK_Struct,
                                   decl_ctx, clang::SourceLocation(),
                                   clang::SourceLocation(), id, prev_decl);
}

clang::Expr *CreateFloatingLiteral(clang::ASTContext &ctx, llvm::APFloat val,
                                   clang::QualType type) {
  return clang::FloatingLiteral::Create(ctx, val, /*isexact=*/true, type,
                                        clang::SourceLocation());
}

clang::Expr *CreateIntegerLiteral(clang::ASTContext &ctx, llvm::APInt val,
                                  clang::QualType type) {
  CHECK_EQ(val.getBitWidth(), ctx.getIntWidth(type));
  return clang::IntegerLiteral::Create(ctx, val, type, clang::SourceLocation());
}

clang::Expr *CreateTrueExpr(clang::ASTContext &ctx) {
  auto type = ctx.UnsignedIntTy;
  auto val = llvm::APInt(ctx.getIntWidth(type), 1);
  return CreateIntegerLiteral(ctx, val, type);
}

clang::Expr *CreateCharacterLiteral(clang::ASTContext &ctx, llvm::APInt val,
                                    clang::QualType type) {
  return new (ctx) clang::CharacterLiteral(
      val.getLimitedValue(), clang::CharacterLiteral::CharacterKind::Ascii,
      type, clang::SourceLocation());
}

clang::Expr *CreateStringLiteral(clang::ASTContext &ctx, std::string val,
                                 clang::QualType type) {
  return clang::StringLiteral::Create(
      ctx, val, clang::StringLiteral::StringKind::Ascii,
      /*Pascal=*/false, type, clang::SourceLocation());
}

clang::Expr *CreateInitListExpr(clang::ASTContext &ctx,
                                std::vector<clang::Expr *> &exprs,
                                clang::QualType type) {
  auto init = new (ctx) clang::InitListExpr(ctx, clang::SourceLocation(), exprs,
                                            clang::SourceLocation());
  init->setType(type);
  return init;
}

clang::Expr *CreateArraySubscriptExpr(clang::ASTContext &ctx, clang::Expr *base,
                                      clang::Expr *idx, clang::QualType type) {
  return new (ctx)
      clang::ArraySubscriptExpr(base, idx, type, clang::VK_RValue,
                                clang::OK_Ordinary, clang::SourceLocation());
}

clang::Expr *CreateCStyleCastExpr(clang::ASTContext &ctx, clang::QualType type,
                                  clang::CastKind cast, clang::Expr *op) {
  return clang::CStyleCastExpr::Create(
      ctx, type, clang::VK_RValue, cast, op, nullptr,
      ctx.getTrivialTypeSourceInfo(type), clang::SourceLocation(),
      clang::SourceLocation());
}

clang::Expr *CreateNullPointerExpr(clang::ASTContext &ctx) {
  auto type = ctx.UnsignedIntTy;
  auto val = llvm::APInt::getNullValue(ctx.getTypeSize(type));
  auto zero = CreateIntegerLiteral(ctx, val, type);
  return CreateCStyleCastExpr(ctx, ctx.VoidPtrTy,
                              clang::CastKind::CK_NullToPointer, zero);
}

clang::Expr *CreateUndefExpr(clang::ASTContext &ctx, clang::QualType type) {
  auto null = CreateNullPointerExpr(ctx);
  auto cast = CreateCStyleCastExpr(ctx, ctx.getPointerType(type),
                                   clang::CastKind::CK_NullToPointer, null);
  return CreateUnaryOperator(ctx, clang::UO_Deref, cast, type);
}

clang::Stmt *CreateDeclStmt(clang::ASTContext &ctx, clang::Decl *decl) {
  return new (ctx)
      clang::DeclStmt(clang::DeclGroupRef(decl), clang::SourceLocation(),
                      clang::SourceLocation());
}

clang::Expr *CreateImplicitCastExpr(clang::ASTContext &ctx,
                                    clang::QualType type, clang::CastKind cast,
                                    clang::Expr *op) {
  return clang::ImplicitCastExpr::Create(ctx, type, cast, op, nullptr,
                                         clang::VK_RValue);
}

clang::Expr *CreateConditionalOperatorExpr(clang::ASTContext &ctx,
                                           clang::Expr *cond, clang::Expr *lhs,
                                           clang::Expr *rhs,
                                           clang::QualType type) {
  return new (ctx) clang::ConditionalOperator(
      cond, clang::SourceLocation(), lhs, clang::SourceLocation(), rhs, type,
      clang::VK_RValue, clang::OK_Ordinary);
}

}  // namespace rellic
