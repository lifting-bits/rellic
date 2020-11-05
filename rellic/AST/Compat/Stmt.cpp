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

#include "rellic/AST/Compat/Stmt.h"

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

}  // namespace rellic
