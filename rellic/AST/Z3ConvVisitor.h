/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/RecursiveASTVisitor.h>
#include <z3++.h>

#include <unordered_map>

#include "rellic/AST/ASTBuilder.h"

namespace rellic {

class Z3ConvVisitor : public clang::RecursiveASTVisitor<Z3ConvVisitor> {
 private:
  clang::ASTContext *c_ctx;
  ASTBuilder ast;

  z3::context *z_ctx;

  // Expression maps
  z3::expr_vector z_expr_vec;
  std::unordered_map<clang::Expr *, unsigned> z_expr_map;
  std::unordered_map<unsigned, clang::Expr *> c_expr_map;
  // Declaration maps
  z3::func_decl_vector z_decl_vec;
  std::unordered_map<clang::ValueDecl *, unsigned> z_decl_map;
  std::unordered_map<unsigned, clang::ValueDecl *> c_decl_map;

  void InsertZ3Expr(clang::Expr *c_expr, z3::expr z_expr);
  z3::expr GetZ3Expr(clang::Expr *c_expr);

  void InsertCExpr(z3::expr z_expr, clang::Expr *c_expr);
  clang::Expr *GetCExpr(z3::expr z_expr);

  void InsertZ3Decl(clang::ValueDecl *c_decl, z3::func_decl z_decl);
  z3::func_decl GetZ3Decl(clang::ValueDecl *c_decl);

  void InsertCValDecl(z3::func_decl z_decl, clang::ValueDecl *c_decl);
  clang::ValueDecl *GetCValDecl(z3::func_decl z_decl);

  z3::sort GetZ3Sort(clang::QualType type);

  clang::Expr *CreateLiteralExpr(z3::expr z_expr);

  z3::expr CreateZ3BitwiseCast(z3::expr expr, size_t src, size_t dst,
                               bool sign);

  void VisitZ3Expr(z3::expr z_expr);

  template <typename T>
  bool HandleCastExpr(T *c_cast);
  clang::Expr *HandleZ3Concat(z3::expr z_op);

 public:
  z3::func_decl GetOrCreateZ3Decl(clang::ValueDecl *c_decl);
  z3::expr GetOrCreateZ3Expr(clang::Expr *c_expr);

  clang::Expr *GetOrCreateCExpr(z3::expr z_expr);

  Z3ConvVisitor(clang::ASTUnit &unit, z3::context *z_ctx);
  bool shouldTraversePostOrder() { return true; }

  z3::expr Z3BoolCast(z3::expr expr);
  z3::expr Z3BoolToBVCast(z3::expr expr);

  bool VisitArraySubscriptExpr(clang::ArraySubscriptExpr *sub);
  bool VisitImplicitCastExpr(clang::ImplicitCastExpr *cast);
  bool VisitCStyleCastExpr(clang::CStyleCastExpr *cast);
  bool VisitMemberExpr(clang::MemberExpr *expr);
  bool VisitCallExpr(clang::CallExpr *call);
  bool VisitParenExpr(clang::ParenExpr *parens);
  bool VisitUnaryOperator(clang::UnaryOperator *c_op);
  bool VisitBinaryOperator(clang::BinaryOperator *c_op);
  bool VisitConditionalOperator(clang::ConditionalOperator *c_op);
  bool VisitDeclRefExpr(clang::DeclRefExpr *c_ref);
  bool VisitCharacterLiteral(clang::CharacterLiteral *lit);
  bool VisitIntegerLiteral(clang::IntegerLiteral *lit);
  bool VisitFloatingLiteral(clang::FloatingLiteral *lit);

  bool VisitVarDecl(clang::VarDecl *var);
  bool VisitFieldDecl(clang::FieldDecl *field);
  bool VisitFunctionDecl(clang::FunctionDecl *func);

  // Do not traverse function bodies
  bool TraverseFunctionDecl(clang::FunctionDecl *func) {
    WalkUpFromFunctionDecl(func);
    return true;
  }

  void VisitConstant(z3::expr z_const);
  void VisitUnaryApp(z3::expr z_op);
  void VisitBinaryApp(z3::expr z_op);
  void VisitTernaryApp(z3::expr z_op);
};

}  // namespace rellic
