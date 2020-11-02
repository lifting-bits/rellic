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

#include "rellic/AST/Compat/Expr.h"

#include "rellic/BC/Version.h"

namespace rellic {

clang::UnaryOperator *CreateUnaryOperator(clang::ASTContext &ast_ctx,
                                          clang::UnaryOperatorKind opc,
                                          clang::Expr *op,
                                          clang::QualType res_type) {
#if LLVM_VERSION_NUMBER >= LLVM_VERSION(11, 0)
  return clang::UnaryOperator::Create(
      ast_ctx, op, opc, res_type, clang::VK_RValue, clang::OK_Ordinary,
      clang::SourceLocation(), false, clang::FPOptionsOverride());
#elif LLVM_VERSION_NUMBER >= LLVM_VERSION(7, 0)
  return new (ast_ctx)
      clang::UnaryOperator(op, opc, res_type, clang::VK_RValue,
                           clang::OK_Ordinary, clang::SourceLocation(), false);
#else
  return new (ast_ctx)
      clang::UnaryOperator(op, opc, res_type, clang::VK_RValue,
                           clang::OK_Ordinary, clang::SourceLocation());
#endif
}

clang::BinaryOperator *CreateBinaryOperator(clang::ASTContext &ast_ctx,
                                            clang::BinaryOperatorKind opc,
                                            clang::Expr *lhs, clang::Expr *rhs,
                                            clang::QualType res_type) {
#if LLVM_VERSION_NUMBER >= LLVM_VERSION(11, 0)
  return clang::BinaryOperator::Create(
      ast_ctx, lhs, rhs, opc, res_type, clang::VK_RValue, clang::OK_Ordinary,
      clang::SourceLocation(), clang::FPOptionsOverride());
#elif LLVM_VERSION_NUMBER >= LLVM_VERSION(5, 0)
  return new (ast_ctx) clang::BinaryOperator(
      lhs, rhs, opc, res_type, clang::VK_RValue, clang::OK_Ordinary,
      clang::SourceLocation(), clang::FPOptions());
#else
  return new (ast_ctx)
      clang::BinaryOperator(lhs, rhs, opc, res_type, clang::VK_RValue,
                            clang::OK_Ordinary, clang::SourceLocation(),
                            /*fpContractable=*/false);
#endif
}

clang::Expr *CreateCallExpr(clang::ASTContext &ctx, clang::Expr *func,
                            std::vector<clang::Expr *> &args,
                            clang::QualType res_type) {
#if LLVM_VERSION_NUMBER >= LLVM_VERSION(8, 0)
  return clang::CallExpr::Create(ctx, func, args, res_type, clang::VK_RValue,
                                 clang::SourceLocation());
#else
  return new (ctx) clang::CallExpr(ctx, func, args, res_type, clang::VK_RValue,
                                   clang::SourceLocation());
#endif
}

clang::Expr *CreateMemberExpr(clang::ASTContext &ctx, clang::Expr *base,
                              clang::ValueDecl *member, clang::QualType type,
                              bool is_arrow) {
#if LLVM_VERSION_NUMBER >= LLVM_VERSION(9, 0)
  return clang::MemberExpr::CreateImplicit(
      ctx, base, is_arrow, member, type, clang::VK_RValue, clang::OK_Ordinary);
#else
  return new (ctx) clang::MemberExpr(base, is_arrow, clang::SourceLocation(),
                                     member, clang::SourceLocation(), type,
                                     clang::VK_RValue, clang::OK_Ordinary);
#endif
}

}  // namespace rellic
