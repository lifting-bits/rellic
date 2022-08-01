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

#include <algorithm>
#include <numeric>
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
    if (IsConstant(expr)) {
      return;
    }

    if (expr.is_and()) {
      for (auto e : expr.args()) {
        AddExpr(e, vec);
      }
      return;
    }

    vec.push_back(expr);
  }

  z3::expr Simplify(z3::expr expr) {
    return HeavySimplify(provenance.z3_ctx, expr);
  }

  z3::expr ApplyAssumptions(z3::expr expr, z3::expr_vector& true_exprs) {
    z3::expr_vector dest{provenance.z3_ctx};
    for (auto e : true_exprs) {
      dest.push_back(provenance.z3_ctx.bool_val(true));
    }

    return expr.substitute(true_exprs, dest);
  }

  z3::expr Sort(z3::expr expr) {
    if (expr.is_and() || expr.is_or()) {
      auto args{expr.args()};
      std::vector<unsigned> args_indices{args.size()};
      std::iota(args_indices.begin(), args_indices.end(), 0);
      std::sort(args_indices.begin(), args_indices.end(),
                [&args](unsigned a, unsigned b) {
                  return args[a].id() < args[b].id();
                });
      z3::expr_vector new_args{provenance.z3_ctx};
      for (auto idx : args_indices) {
        new_args.push_back(args[idx]);
      }
      if (expr.is_and()) {
        return z3::mk_and(new_args);
      } else {
        return z3::mk_or(new_args);
      }
    }

    if (expr.is_not()) {
      return !Sort(expr.arg(0));
    }

    return expr;
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
      auto old_cond{Sort(provenance.z3_exprs[provenance.conds[while_stmt]])};
      auto new_cond{Sort(Simplify(ApplyAssumptions(old_cond, true_exprs)))};
      if (!z3::eq(old_cond, new_cond)) {
        provenance.conds[while_stmt] = provenance.z3_exprs.size();
        provenance.z3_exprs.push_back(new_cond);
        changed = true;
      }
    }

    auto cond{provenance.z3_exprs[provenance.conds[while_stmt]]};

    auto inner{Clone(true_exprs)};
    AddExpr(cond, inner);
    changed |= Visit(while_stmt->getBody(), inner);

    AddExpr(!cond, true_exprs);
    return changed;
  }

  bool VisitDoStmt(clang::DoStmt* do_stmt, z3::expr_vector& true_exprs) {
    bool changed{false};
    if (do_stmt->getCond() == provenance.marker_expr) {
      auto old_cond{provenance.z3_exprs[provenance.conds[do_stmt]]};
      auto new_cond{Sort(Simplify(ApplyAssumptions(old_cond, true_exprs)))};
      if (!z3::eq(old_cond, new_cond)) {
        provenance.conds[do_stmt] = provenance.z3_exprs.size();
        provenance.z3_exprs.push_back(new_cond);
        changed = true;
      }
    }

    auto cond{provenance.z3_exprs[provenance.conds[do_stmt]]};
    auto inner{Clone(true_exprs)};

    Visit(do_stmt->getBody(), inner);

    AddExpr(!cond, true_exprs);
    return changed;
  }

  bool VisitIfStmt(clang::IfStmt* if_stmt, z3::expr_vector& true_exprs) {
    bool changed{false};
    if (if_stmt->getCond() == provenance.marker_expr) {
      auto old_cond{provenance.z3_exprs[provenance.conds[if_stmt]]};
      auto new_cond{Sort(Simplify(ApplyAssumptions(old_cond, true_exprs)))};
      if (!z3::eq(old_cond, new_cond)) {
        provenance.conds[if_stmt] = provenance.z3_exprs.size();
        provenance.z3_exprs.push_back(new_cond);
        changed = true;
      }
    }

    auto cond{provenance.z3_exprs[provenance.conds[if_stmt]]};
    auto inner_then{Clone(true_exprs)};
    AddExpr(cond, inner_then);
    Visit(if_stmt->getThen(), inner_then);

    if (if_stmt->getElse()) {
      auto inner_else{Clone(true_exprs)};
      AddExpr(!cond, inner_else);
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