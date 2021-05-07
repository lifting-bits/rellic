/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Frontend/CompilerInstance.h>
#include <llvm/Support/Host.h>

#include <unordered_map>

#include "rellic/AST/Compat/Expr.h"
#include "rellic/AST/Compat/Stmt.h"

namespace rellic {

using StmtMap = std::unordered_map<clang::Stmt *, clang::Stmt *>;

bool ReplaceChildren(clang::Stmt *stmt, StmtMap &repl_map);

template <typename T>
size_t GetNumDecls(clang::DeclContext *decl_ctx) {
  size_t result = 0;
  for (auto decl : decl_ctx->decls()) {
    if (clang::isa<T>(decl)) {
      ++result;
    }
  }
  return result;
}

clang::QualType GetLeastIntTypeForBitWidth(clang::ASTContext &ctx,
                                           unsigned size, unsigned sign);

// clang::Expr *CastExpr(clang::ASTContext &ctx, clang::QualType dst,
//                          clang::Expr *op);

// clang::IdentifierInfo *CreateIdentifier(clang::ASTContext &ctx,
//                                         std::string name);

clang::DoStmt *CreateDoStmt(clang::ASTContext &ctx, clang::Expr *cond,
                            clang::Stmt *body);

clang::BreakStmt *CreateBreakStmt(clang::ASTContext &ctx);

// clang::DeclRefExpr *CreateDeclRefExpr(clang::ASTContext &ctx,
//                                       clang::ValueDecl *val);

// clang::ParenExpr *CreateParenExpr(clang::ASTContext &ctx, clang::Expr *expr);

// clang::Expr *CreateNotExpr(clang::ASTContext &ctx, clang::Expr *op);

// clang::Expr *CreateAndExpr(clang::ASTContext &ctx, clang::Expr *lhs,
//                            clang::Expr *rhs);

// clang::Expr *CreateOrExpr(clang::ASTContext &ctx, clang::Expr *lhs,
//                           clang::Expr *rhs);

// clang::VarDecl *CreateVarDecl(clang::ASTContext &ctx,
//                               clang::DeclContext *decl_ctx,
//                               clang::IdentifierInfo *id, clang::QualType type);

// clang::ParmVarDecl *CreateParmVarDecl(clang::ASTContext &ctx,
//                                       clang::DeclContext *decl_ctx,
//                                       clang::IdentifierInfo *id,
//                                       clang::QualType type);

// clang::FunctionDecl *CreateFunctionDecl(clang::ASTContext &ctx,
//                                         clang::DeclContext *decl_ctx,
//                                         clang::IdentifierInfo *id,
//                                         clang::QualType type);

// clang::FieldDecl *CreateFieldDecl(clang::ASTContext &ctx,
//                                   clang::DeclContext *decl_ctx,
//                                   clang::IdentifierInfo *id,
//                                   clang::QualType type);

// clang::RecordDecl *CreateStructDecl(clang::ASTContext &ctx,
//                                     clang::DeclContext *decl_ctx,
//                                     clang::IdentifierInfo *id,
//                                     clang::RecordDecl *prev_decl = nullptr);

// clang::Expr *CreateFloatingLiteral(clang::ASTContext &ctx, llvm::APFloat val,
//                                    clang::QualType type);

// clang::Expr *CreateIntegerLiteral(clang::ASTContext &ctx, llvm::APInt val,
//                                   clang::QualType type);

// clang::Expr *CreateTrueExpr(clang::ASTContext &ctx);

// clang::Expr *CreateCharacterLiteral(clang::ASTContext &ctx, llvm::APInt val,
//                                     clang::QualType type);

// clang::Expr *CreateStringLiteral(clang::ASTContext &ctx, std::string val,
//                                  clang::QualType);

clang::Expr *CreateInitListExpr(clang::ASTContext &ctx,
                                std::vector<clang::Expr *> &exprs,
                                clang::QualType type);

// clang::Expr *CreateArraySubscriptExpr(clang::ASTContext &ctx, clang::Expr *base,
//                                       clang::Expr *idx, clang::QualType type);

// clang::Expr *CreateMemberExpr(clang::ASTContext &ctx, clang::Expr *base,
//                               clang::ValueDecl *member, clang::QualType type,
//                               bool is_arrow = false);

// clang::Expr *CreateNullPointerExpr(clang::ASTContext &ctx);

// clang::Expr *CreateUndefExpr(clang::ASTContext &ctx, clang::QualType type);

// clang::Expr *CreateCStyleCastExpr(clang::ASTContext &ctx, clang::QualType type,
//                                   clang::CastKind cast, clang::Expr *op);

// clang::Stmt *CreateDeclStmt(clang::ASTContext &ctx, clang::Decl *decl);

// clang::Expr *CreateImplicitCastExpr(clang::ASTContext &ctx,
//                                     clang::QualType type, clang::CastKind cast,
//                                     clang::Expr *op);

clang::Expr *CreateConditionalOperatorExpr(clang::ASTContext &ctx,
                                           clang::Expr *cond, clang::Expr *lhs,
                                           clang::Expr *rhs,
                                           clang::QualType type);

}  // namespace rellic