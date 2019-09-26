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

#include <clang/AST/RecursiveASTVisitor.h>

#include <z3++.h>

#include <unordered_map>

namespace rellic {

class Z3ConvVisitor : public clang::RecursiveASTVisitor<Z3ConvVisitor> {
 private:
  clang::ASTContext *ast_ctx;
  z3::context *z3_ctx;

  // Expression maps
  z3::expr_vector z3_expr_vec;
  std::unordered_map<clang::Expr *, unsigned> z3_expr_map;
  std::unordered_map<unsigned, clang::Expr *> c_expr_map;
  // Declaration maps
  z3::func_decl_vector z3_decl_vec;
  std::unordered_map<clang::ValueDecl *, unsigned> z3_decl_map;
  std::unordered_map<unsigned, clang::ValueDecl *> c_decl_map;

  void InsertZ3Expr(clang::Expr *c_expr, z3::expr z3_expr);
  z3::expr GetZ3Expr(clang::Expr *c_expr);

  void InsertCExpr(z3::expr z3_expr, clang::Expr *c_expr);
  clang::Expr *GetCExpr(z3::expr z3_expr);

  void InsertZ3Decl(clang::ValueDecl *c_decl, z3::func_decl z3_decl);
  z3::func_decl GetZ3Decl(clang::ValueDecl *c_decl);

  void InsertCValDecl(z3::func_decl z3_decl, clang::ValueDecl *c_decl);
  clang::ValueDecl *GetCValDecl(z3::func_decl z3_decl);

  z3::sort GetZ3Sort(clang::QualType type);

  clang::QualType GetQualType(z3::sort z3_sort);

  clang::Expr *CreateLiteralExpr(z3::expr z3_expr);
  
  void VisitZ3Expr(z3::expr z3_expr);

 public:
  z3::func_decl GetOrCreateZ3Decl(clang::ValueDecl *c_decl);
  z3::expr GetOrCreateZ3Expr(clang::Expr *c_expr);

  clang::Expr *GetOrCreateCExpr(z3::expr z3_expr);

  Z3ConvVisitor(clang::ASTContext *c_ctx, z3::context *z3_ctx);
  bool shouldTraversePostOrder() { return true; }

  z3::expr Z3BoolCast(z3::expr expr);

  bool VisitArraySubscriptExpr(clang::ArraySubscriptExpr *sub);
  bool VisitImplicitCastExpr(clang::ImplicitCastExpr *cast);
  bool VisitCStyleCastExpr(clang::CStyleCastExpr *cast);
  bool VisitMemberExpr(clang::MemberExpr *expr);
  bool VisitCallExpr(clang::CallExpr *call);
  bool VisitParenExpr(clang::ParenExpr *parens);
  bool VisitUnaryOperator(clang::UnaryOperator *c_op);
  bool VisitBinaryOperator(clang::BinaryOperator *c_op);
  bool VisitDeclRefExpr(clang::DeclRefExpr *c_ref);
  bool VisitIntegerLiteral(clang::IntegerLiteral *c_lit);

  bool VisitVarDecl(clang::VarDecl *var);
  bool VisitFieldDecl(clang::FieldDecl *field);
  bool VisitFunctionDecl(clang::FunctionDecl *func);

  // Do not traverse function bodies
  bool TraverseFunctionDecl(clang::FunctionDecl *func) {
    WalkUpFromFunctionDecl(func);
    return true;
  }

  void VisitConstant(z3::expr z3_const);
  void VisitUnaryApp(z3::expr z3_op);
  void VisitBinaryApp(z3::expr z3_op);
};

}  // namespace rellic
