/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/Stmt.h>
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/APSInt.h>

namespace rellic {

clang::WhileStmt *CreateWhileStmt(clang::ASTContext &ctx, clang::Expr *cond,
                                  clang::Stmt *body);

clang::IfStmt *CreateIfStmt(clang::ASTContext &ctx, clang::Expr *cond,
                            clang::Stmt *then);

clang::CompoundStmt *CreateCompoundStmt(clang::ASTContext &ctx,
                                        std::vector<clang::Stmt *> &stmts);

clang::ReturnStmt *CreateReturnStmt(clang::ASTContext &ctx, clang::Expr *expr);

clang::Optional<llvm::APSInt> GetIntegerConstantExprFromIf(clang::IfStmt *ifstmt, const clang::ASTContext &ctx);


}  // namespace rellic
