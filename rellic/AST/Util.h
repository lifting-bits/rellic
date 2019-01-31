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

#ifndef RELLIC_AST_UTIL_H_
#define RELLIC_AST_UTIL_H_

#include <clang/AST/ASTContext.h>
#include <clang/AST/Expr.h>

#include <unordered_map>

namespace rellic {

using StmtMap = std::unordered_map<clang::Stmt *, clang::Stmt *>;

bool ReplaceChildren(clang::Stmt *stmt, StmtMap &repl_map);

clang::IdentifierInfo *CreateIdentifier(clang::ASTContext &ctx,
                                        std::string name);

clang::DeclRefExpr *CreateDeclRefExpr(clang::ASTContext &ctx,
                                      clang::ValueDecl *val);

clang::CompoundStmt *CreateCompoundStmt(clang::ASTContext &ctx,
                                        std::vector<clang::Stmt *> &stmts);

clang::IfStmt *CreateIfStmt(clang::ASTContext &ctx, clang::Expr *cond,
                            clang::Stmt *then);

clang::WhileStmt *CreateWhileStmt(clang::ASTContext &ctx, clang::Expr *cond,
                                  clang::Stmt *body);

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

clang::BinaryOperator *CreateBinaryOperator(clang::ASTContext &ctx,
                                            clang::BinaryOperatorKind opc,
                                            clang::Expr *lhs, clang::Expr *rhs,
                                            clang::QualType res_type);

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
                                    clang::IdentifierInfo *id);

clang::Expr *CreateTrueExpr(clang::ASTContext &ctx);

}  // namespace rellic

#endif  // RELLIC_AST_UTIL_H_