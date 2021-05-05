/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/Expr.h>

namespace rellic {

// clang::UnaryOperator *CreateUnaryOperator(clang::ASTContext &ast_ctx,
//                                           clang::UnaryOperatorKind opc,
//                                           clang::Expr *expr,
//                                           clang::QualType res_type);

// clang::BinaryOperator *CreateBinaryOperator(clang::ASTContext &ast_ctx,
//                                             clang::BinaryOperatorKind opc,
//                                             clang::Expr *lhs, clang::Expr *rhs,
//                                             clang::QualType res_type);

// clang::Expr *CreateCallExpr(clang::ASTContext &ctx, clang::Expr *func,
//                             std::vector<clang::Expr *> &args,
//                             clang::QualType res_type);

clang::Expr *CreateMemberExpr(clang::ASTContext &ctx, clang::Expr *base,
                              clang::ValueDecl *member, clang::QualType type,
                              bool is_arrow);

}  // namespace rellic