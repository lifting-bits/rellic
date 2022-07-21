/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/NestedCondProp.h"

#include <clang/AST/StmtVisitor.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <z3++.h>

#include <vector>

#include "rellic/AST/ASTBuilder.h"
#include "rellic/AST/Util.h"

namespace rellic {
class CompoundVisitor
    : public clang::StmtVisitor<CompoundVisitor, bool, z3::expr_vector&> {
 private:
  Provenance& provenance;
  ASTBuilder& ast;
  clang::ASTContext& ctx;

  bool IsConstant(z3::expr expr) {
    if (Prove(provenance.z3_ctx, expr)) {
      return true;
    }

    if (Prove(provenance.z3_ctx, !expr)) {
      return true;
    }

    return false;
  }

  void AddExpr(z3::expr expr, z3::expr_vector& vec) {
    if (!IsConstant(expr)) {
      vec.push_back(expr);
    }
  }

  z3::expr Simplify(z3::expr expr) {
    return HeavySimplify(provenance.z3_ctx, expr);
  }

  z3::expr SimplifyWithAssumptions(z3::expr expr, z3::expr_vector& true_exprs) {
    auto true_expr{z3::mk_and(true_exprs)};
    auto decl_kind{expr.decl().decl_kind()};

    if (Prove(provenance.z3_ctx, z3::implies(true_expr, expr))) {
      return provenance.z3_ctx.bool_val(true);
    }

    if (Prove(provenance.z3_ctx, z3::implies(true_expr, !expr))) {
      return provenance.z3_ctx.bool_val(false);
    }

    if (!expr.is_app() || decl_kind == Z3_OP_UNINTERPRETED ||
        decl_kind == Z3_OP_NOT || decl_kind == Z3_OP_TRUE ||
        decl_kind == Z3_OP_FALSE) {
      return expr;
    }

    if (decl_kind == Z3_OP_OR) {
      z3::expr_vector new_or{provenance.z3_ctx};
      for (auto sub : expr.args()) {
        if (Prove(provenance.z3_ctx, z3::implies(true_exprs, sub))) {
          return provenance.z3_ctx.bool_val(true);
        }

        if (!Prove(provenance.z3_ctx, z3::implies(true_exprs, !expr))) {
          new_or.push_back(sub);
        }
      }

      return z3::mk_or(new_or).simplify();
    }

    CHECK_EQ(decl_kind, Z3_OP_AND)
        << "Unknown expression kind: " << expr.to_string();

    z3::expr_vector new_conj{provenance.z3_ctx};
    for (auto sub : expr.args()) {
      auto sub_decl_kind{sub.decl().decl_kind()};

      if (sub_decl_kind == Z3_OP_TRUE) {
        continue;
      }

      if (sub_decl_kind == Z3_OP_FALSE) {
        return sub;
      }

      if (sub_decl_kind == Z3_OP_OR) {
        z3::expr_vector new_disj{provenance.z3_ctx};
        for (auto sub_disj : sub.args()) {
          if (Prove(provenance.z3_ctx, z3::implies(true_expr, !sub_disj))) {
            continue;
          }

          if (Prove(provenance.z3_ctx, z3::implies(true_expr, sub_disj))) {
            new_disj.push_back(provenance.z3_ctx.bool_val(true));
          }

          new_disj.push_back(sub_disj);
        }

        sub = z3::mk_or(new_disj).simplify();
      }

      if (Prove(provenance.z3_ctx, z3::implies(true_expr, !sub))) {
        return provenance.z3_ctx.bool_val(false);
      }

      if (Prove(provenance.z3_ctx, z3::implies(true_expr, sub))) {
        continue;
      }

      new_conj.push_back(sub);
    }

    return HeavySimplify(provenance.z3_ctx, z3::mk_and(new_conj));
  }

 public:
  CompoundVisitor(Provenance& provenance, ASTBuilder& ast,
                  clang::ASTContext& ctx)
      : provenance(provenance), ast(ast), ctx(ctx) {}

  bool VisitCompoundStmt(clang::CompoundStmt* compound,
                         z3::expr_vector& true_exprs) {
    bool changed{false};
    for (auto stmt : compound->body()) {
      changed |= Visit(stmt, true_exprs);
    }

    return changed;
  }

  bool VisitWhileStmt(clang::WhileStmt* while_stmt,
                      z3::expr_vector& true_exprs) {
    bool changed{false};
    if (while_stmt->getCond() == provenance.marker_expr) {
      auto old_cond{provenance.z3_exprs[provenance.conds[while_stmt]]};
      auto new_cond{SimplifyWithAssumptions(old_cond, true_exprs)};
      if (!z3::eq(old_cond, new_cond)) {
        provenance.conds[while_stmt] = provenance.z3_exprs.size();
        provenance.z3_exprs.push_back(new_cond);
        changed = true;
      }
    }

    auto cond{provenance.z3_exprs[provenance.conds[while_stmt]]};

    auto inner{Clone(true_exprs)};
    auto simplified_cond{SimplifyWithAssumptions(cond, true_exprs)};
    AddExpr(simplified_cond, inner);
    changed |= Visit(while_stmt->getBody(), inner);

    AddExpr(!cond, true_exprs);
    return changed;
  }

  bool VisitDoStmt(clang::DoStmt* do_stmt, z3::expr_vector& true_exprs) {
    bool changed{false};
    if (do_stmt->getCond() == provenance.marker_expr) {
      auto old_cond{provenance.z3_exprs[provenance.conds[do_stmt]]};
      auto new_cond{SimplifyWithAssumptions(old_cond, true_exprs)};
      if (!z3::eq(old_cond, new_cond)) {
        provenance.conds[do_stmt] = provenance.z3_exprs.size();
        provenance.z3_exprs.push_back(new_cond);
        changed = true;
      }
    }

    auto cond{provenance.z3_exprs[provenance.conds[do_stmt]]};
    auto inner{Clone(true_exprs)};

    Visit(do_stmt->getBody(), inner);

    auto simplified_cond{SimplifyWithAssumptions(cond, true_exprs)};
    AddExpr(!simplified_cond, true_exprs);
    return changed;
  }

  bool VisitIfStmt(clang::IfStmt* if_stmt, z3::expr_vector& true_exprs) {
    bool changed{false};
    if (if_stmt->getCond() == provenance.marker_expr) {
      auto old_cond{provenance.z3_exprs[provenance.conds[if_stmt]]};
      auto new_cond{SimplifyWithAssumptions(old_cond, true_exprs)};
      if (!z3::eq(old_cond, new_cond)) {
        provenance.conds[if_stmt] = provenance.z3_exprs.size();
        provenance.z3_exprs.push_back(new_cond);
        changed = true;
      }
    }

    auto cond{provenance.z3_exprs[provenance.conds[if_stmt]]};
    auto inner_then{Clone(true_exprs)};
    auto simplified_cond{SimplifyWithAssumptions(cond, true_exprs)};
    AddExpr(simplified_cond, inner_then);
    Visit(if_stmt->getThen(), inner_then);

    if (if_stmt->getElse()) {
      auto inner_else{Clone(true_exprs)};
      AddExpr(!simplified_cond, inner_else);
      Visit(if_stmt->getElse(), inner_else);
    }
    return changed;
  }
};

NestedCondProp::NestedCondProp(Provenance& provenance, clang::ASTUnit& unit)
    : ASTPass(provenance, unit) {}

void NestedCondProp::RunImpl() {
  changed = false;
  ASTBuilder ast{ast_unit};
  CompoundVisitor visitor{provenance, ast, ast_ctx};

  for (auto decl : ast_ctx.getTranslationUnitDecl()->decls()) {
    if (auto fdecl = clang::dyn_cast<clang::FunctionDecl>(decl)) {
      if (fdecl->hasBody()) {
        z3::expr_vector true_exprs{provenance.z3_ctx};
        changed |= visitor.Visit(fdecl->getBody(), true_exprs);
      }
    }
  }
}
}  // namespace rellic