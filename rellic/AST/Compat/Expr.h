/*
 * Copyright (c) 2019 Trail of Bits, Inc.
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

#include <clang/AST/Expr.h>

namespace rellic {

clang::UnaryOperator *CreateUnaryOperator(clang::ASTContext &ast_ctx,
                                          clang::UnaryOperatorKind opc,
                                          clang::Expr *expr,
                                          clang::QualType res_type);

clang::BinaryOperator *CreateBinaryOperator(clang::ASTContext &ast_ctx,
                                            clang::BinaryOperatorKind opc,
                                            clang::Expr *lhs, clang::Expr *rhs,
                                            clang::QualType res_type);

clang::CompoundStmt *CreateCompoundStmt(clang::ASTContext &ctx,
                                        std::vector<clang::Stmt *> &stmts);

clang::ReturnStmt *CreateReturnStmt(clang::ASTContext &ctx, clang::Expr *expr);

clang::Expr *CreateCallExpr(clang::ASTContext &ctx, clang::Expr *func,
                            std::vector<clang::Expr *> &args,
                            clang::QualType res_type);

clang::WhileStmt *CreateWhileStmt(clang::ASTContext &ctx, clang::Expr *cond,
                                  clang::Stmt *body);

clang::IfStmt *CreateIfStmt(clang::ASTContext &ctx, clang::Expr *cond,
                            clang::Stmt *then);

}  // namespace rellic