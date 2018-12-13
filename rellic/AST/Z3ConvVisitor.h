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

#ifndef RELLIC_AST_Z3CONVVISITOR_H_
#define RELLIC_AST_Z3CONVVISITOR_H_

#include <clang/AST/RecursiveASTVisitor.h>

#include <z3++.h>

#include <unordered_map>

namespace rellic {

class Z3ConvVisitor
    : public clang::RecursiveASTVisitor<Z3ConvVisitor> {
 private:
    clang::ASTContext *ast_ctx;
    z3::context *z3_ctx;
    
    z3::expr_vector z3_expr_vec;
    // Expression maps
    std::unordered_map<clang::Expr *, unsigned> z3_expr_map;
    std::unordered_map<unsigned, clang::Expr *> c_expr_map;
    // Z3 constant to C variable map
    std::unordered_map<std::string, clang::ValueDecl *> c_var_map;
    
    void InsertZ3Expr(clang::Expr *c_expr, z3::expr z3_expr);
    z3::expr GetZ3Expr(clang::Expr *c_expr);

    void InsertCExpr(z3::expr z3_expr, clang::Expr *c_expr);
    clang::Expr *GetCExpr(z3::expr z3_expr);
    clang::ValueDecl *GetCVar(std::string var_name);
    
    void VisitZ3Expr(z3::expr z3_expr);
    
 public:
    z3::expr GetOrCreateZ3Expr(clang::Expr *c_expr);
    clang::Expr *GetOrCreateCExpr(z3::expr z3_expr);

    void DeclareCVar(clang::ValueDecl *c_decl);

    Z3ConvVisitor(clang::ASTContext *c_ctx, z3::context *z3_ctx);
    bool shouldTraversePostOrder() { return true; }

    z3::expr Z3BoolCast(z3::expr expr);

    bool VisitParenExpr(clang::ParenExpr *parens);
    bool VisitUnaryOperator(clang::UnaryOperator *c_op);
    bool VisitBinaryOperator(clang::BinaryOperator *c_op);
    bool VisitDeclRefExpr(clang::DeclRefExpr *c_ref);
    bool VisitIntegerLiteral(clang::IntegerLiteral *c_lit);

    void VisitConstant(z3::expr z3_const);
    void VisitUnaryApp(z3::expr z3_op);
    void VisitBinaryApp(z3::expr z3_op);
};

}  // namespace rellic

#endif  // RELLIC_AST_Z3CONVVISITOR_H_
