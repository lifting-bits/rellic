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

#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Frontend/CompilerInstance.h>

#include <unordered_map>

#include "rellic/AST/Compat/Expr.h"
#include "rellic/AST/Compat/Stmt.h"

namespace rellic {

void InitCompilerInstance(
    clang::CompilerInstance &ins,
    std::string target_triple = llvm::sys::getDefaultTargetTriple());

using StmtMap = std::unordered_map<clang::Stmt *, clang::Stmt *>;

void InitCompilerInstance(clang::CompilerInstance &ins,
                          std::string target_triple);

bool ReplaceChildren(clang::Stmt *stmt, StmtMap &repl_map);

clang::IdentifierInfo *CreateIdentifier(clang::ASTContext &ctx,
                                        std::string name);

clang::DeclRefExpr *CreateDeclRefExpr(clang::ASTContext &ctx,
                                      clang::ValueDecl *val);

clang::DoStmt *CreateDoStmt(clang::ASTContext &ctx, clang::Expr *cond,
                            clang::Stmt *body);

clang::BreakStmt *CreateBreakStmt(clang::ASTContext &ctx);

clang::DeclRefExpr *CreateDeclRefExpr(clang::ASTContext &ctx,
                                      clang::ValueDecl *val);

clang::ParenExpr *CreateParenExpr(clang::ASTContext &ctx, clang::Expr *expr);

clang::Expr *CreateNotExpr(clang::ASTContext &ctx, clang::Expr *op);

clang::Expr *CreateAndExpr(clang::ASTContext &ctx, clang::Expr *lhs,
                           clang::Expr *rhs);

clang::Expr *CreateOrExpr(clang::ASTContext &ctx, clang::Expr *lhs,
                          clang::Expr *rhs);

clang::ParmVarDecl *CreateParmVarDecl(clang::ASTContext &ctx,
                                      clang::DeclContext *decl_ctx,
                                      clang::IdentifierInfo *id,
                                      clang::QualType type);

clang::FunctionDecl *CreateFunctionDecl(clang::ASTContext &ctx,
                                        clang::DeclContext *decl_ctx,
                                        clang::IdentifierInfo *id,
                                        clang::QualType type);

clang::FieldDecl *CreateFieldDecl(clang::ASTContext &ctx,
                                  clang::DeclContext *decl_ctx,
                                  clang::IdentifierInfo *id,
                                  clang::QualType type);

clang::RecordDecl *CreateStructDecl(clang::ASTContext &ctx,
                                    clang::DeclContext *decl_ctx,
                                    clang::IdentifierInfo *id,
                                    clang::RecordDecl *prev_decl = nullptr);

clang::Expr *CreateFloatingLiteral(clang::ASTContext &ctx, llvm::APFloat &val,
                                   clang::QualType type);

clang::Expr *CreateIntegerLiteral(clang::ASTContext &ctx, llvm::APInt &val,
                                  clang::QualType type);

clang::Expr *CreateTrueExpr(clang::ASTContext &ctx);

clang::Expr *CreateStringLiteral(clang::ASTContext &ctx, std::string val,
                                 clang::QualType);

clang::Expr *CreateInitListExpr(clang::ASTContext &ctx,
                                std::vector<clang::Expr *> &exprs);

clang::Expr *CreateArraySubscriptExpr(clang::ASTContext &ctx, clang::Expr *base,
                                      clang::Expr *idx, clang::QualType type);

clang::Expr *CreateMemberExpr(clang::ASTContext &ctx, clang::Expr *base,
                              clang::ValueDecl *member, clang::QualType type,
                              bool is_arrow = false);

clang::Expr *CreateNullPointerExpr(clang::ASTContext &ctx);

}  // namespace rellic