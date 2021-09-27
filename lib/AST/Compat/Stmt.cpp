/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/Compat/Stmt.h"

#include "clang/AST/Expr.h"
#include "llvm/ADT/APSInt.h"
#include "rellic/BC/Version.h"

namespace rellic {

clang::IfStmt *CreateIfStmt(clang::ASTContext &ctx, clang::Expr *cond,
                            clang::Stmt *then) {
#if LLVM_VERSION_NUMBER >= LLVM_VERSION(8, 0)
  auto ifstmt = clang::IfStmt::CreateEmpty(ctx, /*HasElse=*/true,
                                           /*HasVar=*/false, /*HasInit=*/false);
  ifstmt->setCond(cond);
  ifstmt->setThen(then);
  ifstmt->setElse(nullptr);
  return ifstmt;
#else
  return new (ctx)
      clang::IfStmt(ctx, clang::SourceLocation(), /*IsConstexpr=*/false,
                    /*init=*/nullptr,
                    /*var=*/nullptr, cond, then);
#endif
}

clang::WhileStmt *CreateWhileStmt(clang::ASTContext &ctx, clang::Expr *cond,
                                  clang::Stmt *body) {
#if LLVM_VERSION_NUMBER >= LLVM_VERSION(11, 0)
  return clang::WhileStmt::Create(
      ctx, nullptr, cond, body, clang::SourceLocation(),
      clang::SourceLocation(), clang::SourceLocation());
#elif LLVM_VERSION_NUMBER >= LLVM_VERSION(8, 0)
  return clang::WhileStmt::Create(ctx, nullptr, cond, body,
                                  clang::SourceLocation());
#else
  return new (ctx)
      clang::WhileStmt(ctx, nullptr, cond, body, clang::SourceLocation());
#endif
}

clang::ReturnStmt *CreateReturnStmt(clang::ASTContext &ctx, clang::Expr *expr) {
#if LLVM_VERSION_NUMBER >= LLVM_VERSION(8, 0)
  return clang::ReturnStmt::Create(ctx, clang::SourceLocation(), expr, nullptr);
#else
  return new (ctx) clang::ReturnStmt(clang::SourceLocation(), expr, nullptr);
#endif
}

clang::CompoundStmt *CreateCompoundStmt(clang::ASTContext &ctx,
                                        std::vector<clang::Stmt *> &stmts) {
#if LLVM_VERSION_NUMBER >= LLVM_VERSION(6, 0)
  return clang::CompoundStmt::Create(ctx, stmts, clang::SourceLocation(),
                                     clang::SourceLocation());
#else
  return new (ctx) clang::CompoundStmt(ctx, stmts, clang::SourceLocation(),
                                       clang::SourceLocation());
#endif
}

clang::Optional<llvm::APSInt> GetIntegerConstantExprFromIf(
    clang::IfStmt *ifstmt, const clang::ASTContext &ctx) {
#if LLVM_VERSION_NUMBER >= LLVM_VERSION(12, 0)
  auto v = ifstmt->getCond()->getIntegerConstantExpr(ctx);
  return v;
#else
  llvm::APSInt val;
  bool is_const = ifstmt->getCond()->isIntegerConstantExpr(val, ctx);
  if (!is_const) {
    return clang::None;
  } else {
    return clang::Optional<llvm::APSInt>{val};
  }
#endif
}

}  // namespace rellic
