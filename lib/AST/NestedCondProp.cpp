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
struct KnownExprs {
  z3::expr_vector src;
  z3::expr_vector dst;

  KnownExprs(z3::context& ctx) : src(ctx), dst(ctx) {}
  KnownExprs(z3::expr_vector&& src, z3::expr_vector&& dst)
      : src(std::move(src)), dst(std::move(dst)) {}
  KnownExprs Clone() { return {::rellic::Clone(src), ::rellic::Clone(dst)}; }

  bool IsConstant(z3::expr expr) {
    if (Prove(src.ctx(), expr)) {
      return true;
    }

    if (Prove(src.ctx(), !expr)) {
      return true;
    }

    return false;
  }

  void AddExpr(z3::expr expr, bool value) {
    if (IsConstant(expr)) {
      return;
    }

    if (expr.is_not()) {
      AddExpr(expr.arg(0), !value);
      return;
    }

    if (expr.num_args() == 1) {
      AddExpr(expr.arg(0), value);
      return;
    }

    if (value && expr.is_and()) {
      for (auto e : expr.args()) {
        AddExpr(e, true);
      }
      return;
    }

    if (!value && expr.is_or()) {
      for (auto e : expr.args()) {
        AddExpr(e, false);
      }
      return;
    }

    src.push_back(expr);
    dst.push_back(dst.ctx().bool_val(value));
    CHECK_EQ(src.size(), dst.size());
  }

  z3::expr ApplyAssumptions(z3::expr expr) {
    auto res{expr.substitute(src, dst)};
    return res;
  }
};

class CompoundVisitor
    : public clang::StmtVisitor<CompoundVisitor, bool, KnownExprs&> {
 private:
  Provenance& provenance;
  ASTBuilder& ast;
  clang::ASTContext& ctx;

  z3::expr Simplify(z3::expr expr) {
    return HeavySimplify(provenance.z3_ctx, expr);
  }

  z3::expr Sort(z3::expr expr) {
    if (expr.is_and() || expr.is_or()) {
      std::vector<unsigned> args_indices(expr.num_args(), 0);
      std::iota(args_indices.begin(), args_indices.end(), 0);
      std::sort(args_indices.begin(), args_indices.end(),
                [&expr](unsigned a, unsigned b) {
                  return expr.arg(a).id() < expr.arg(b).id();
                });
      z3::expr_vector new_args{provenance.z3_ctx};
      for (auto idx : args_indices) {
        new_args.push_back(Sort(expr.arg(idx)));
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
                         KnownExprs& known_exprs) {
    bool changed{false};
    for (auto stmt : compound->body()) {
      changed |= Visit(stmt, known_exprs);
    }

    return changed;
  }

  bool VisitWhileStmt(clang::WhileStmt* while_stmt, KnownExprs& known_exprs) {
    auto cond_idx{provenance.conds[while_stmt]};
    auto old_cond{Sort(provenance.z3_exprs[cond_idx])};
    auto new_cond{Sort(known_exprs.ApplyAssumptions(old_cond))};
    if (while_stmt->getCond() != provenance.marker_expr &&
        !z3::eq(old_cond, new_cond)) {
      provenance.z3_exprs.set(cond_idx, new_cond);
      return true;
    }

    bool changed{false};
    auto inner{known_exprs.Clone()};
    inner.AddExpr(new_cond, true);
    changed |= Visit(while_stmt->getBody(), inner);

    known_exprs.AddExpr(new_cond, false);
    return changed;
  }

  bool VisitDoStmt(clang::DoStmt* do_stmt, KnownExprs& known_exprs) {
    auto cond_idx{provenance.conds[do_stmt]};
    auto old_cond{Sort(provenance.z3_exprs[cond_idx])};
    auto new_cond{Sort(known_exprs.ApplyAssumptions(old_cond))};
    if (do_stmt->getCond() == provenance.marker_expr &&
        !z3::eq(old_cond, new_cond)) {
      provenance.z3_exprs.set(cond_idx, new_cond);
      return true;
    }

    bool changed{false};
    auto inner{known_exprs.Clone()};
    changed |= Visit(do_stmt->getBody(), inner);

    known_exprs.AddExpr(new_cond, false);
    return changed;
  }

  bool VisitIfStmt(clang::IfStmt* if_stmt, KnownExprs& known_exprs) {
    auto cond_idx{provenance.conds[if_stmt]};
    auto old_cond{Sort(provenance.z3_exprs[cond_idx])};
    auto new_cond{Sort(known_exprs.ApplyAssumptions(old_cond))};
    if (if_stmt->getCond() == provenance.marker_expr &&
        !z3::eq(old_cond, new_cond)) {
      provenance.z3_exprs.set(cond_idx, new_cond);
      return true;
    }

    bool changed{false};
    auto inner_then{known_exprs.Clone()};
    inner_then.AddExpr(new_cond, true);
    changed |= Visit(if_stmt->getThen(), inner_then);

    if (if_stmt->getElse()) {
      auto inner_else{known_exprs.Clone()};
      inner_else.AddExpr(new_cond, false);
      changed |= Visit(if_stmt->getElse(), inner_else);
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
        KnownExprs known_exprs{provenance.z3_ctx};
        changed |= visitor.Visit(fdecl->getBody(), known_exprs);
      }
    }
  }
}
}  // namespace rellic