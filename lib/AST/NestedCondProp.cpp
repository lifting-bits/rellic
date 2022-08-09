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

  z3::expr ApplyAssumptions(z3::expr expr, bool& found) {
    if (IsConstant(expr)) {
      return expr;
    }

    for (unsigned i{0}; i < dst.size(); ++i) {
      if (z3::eq(expr, src[i])) {
        found = true;
        return dst[i];
      }
    }

    if (expr.is_and() || expr.is_or()) {
      z3::expr_vector args{src.ctx()};
      for (auto arg : expr.args()) {
        args.push_back(ApplyAssumptions(arg, found));
      }
      if (expr.is_and()) {
        return z3::mk_and(args);
      } else {
        return z3::mk_or(args);
      }
    }

    if (expr.is_not()) {
      return !ApplyAssumptions(expr.arg(0), found);
    }

    return expr;
  }
};

class CompoundVisitor
    : public clang::StmtVisitor<CompoundVisitor, bool, KnownExprs&> {
 private:
  Provenance& provenance;
  ASTBuilder& ast;
  clang::ASTContext& ctx;

 public:
  CompoundVisitor(Provenance& provenance, ASTBuilder& ast,
                  clang::ASTContext& ctx)
      : provenance(provenance), ast(ast), ctx(ctx) {}

  bool VisitCompoundStmt(clang::CompoundStmt* compound,
                         KnownExprs& known_exprs) {
    for (auto stmt : compound->body()) {
      if (Visit(stmt, known_exprs)) {
        return true;
      }
    }

    return false;
  }

  bool VisitWhileStmt(clang::WhileStmt* while_stmt, KnownExprs& known_exprs) {
    auto cond_idx{provenance.conds[while_stmt]};
    bool changed{false};
    auto old_cond{provenance.z3_exprs[cond_idx]};
    auto new_cond{known_exprs.ApplyAssumptions(old_cond, changed)};
    if (while_stmt->getCond() != provenance.marker_expr && changed) {
      provenance.z3_exprs.set(cond_idx, new_cond);
      return true;
    }

    auto inner{known_exprs.Clone()};
    inner.AddExpr(new_cond, true);
    known_exprs.AddExpr(new_cond, false);

    if (Visit(while_stmt->getBody(), inner)) {
      return true;
    }
    return false;
  }

  bool VisitDoStmt(clang::DoStmt* do_stmt, KnownExprs& known_exprs) {
    auto cond_idx{provenance.conds[do_stmt]};
    bool changed{false};
    auto old_cond{provenance.z3_exprs[cond_idx]};
    auto new_cond{known_exprs.ApplyAssumptions(old_cond, changed)};
    if (do_stmt->getCond() == provenance.marker_expr && changed) {
      provenance.z3_exprs.set(cond_idx, new_cond);
      return true;
    }

    auto inner{known_exprs.Clone()};
    known_exprs.AddExpr(new_cond, false);

    if (Visit(do_stmt->getBody(), inner)) {
      return true;
    }

    return false;
  }

  bool VisitIfStmt(clang::IfStmt* if_stmt, KnownExprs& known_exprs) {
    auto cond_idx{provenance.conds[if_stmt]};
    bool changed{false};
    auto old_cond{provenance.z3_exprs[cond_idx]};
    auto new_cond{known_exprs.ApplyAssumptions(old_cond, changed)};
    if (if_stmt->getCond() == provenance.marker_expr && changed) {
      provenance.z3_exprs.set(cond_idx, new_cond);
      return true;
    }

    auto inner_then{known_exprs.Clone()};
    inner_then.AddExpr(new_cond, true);
    if (Visit(if_stmt->getThen(), inner_then)) {
      return true;
    }

    if (if_stmt->getElse()) {
      auto inner_else{known_exprs.Clone()};
      inner_else.AddExpr(new_cond, false);
      if (Visit(if_stmt->getElse(), inner_else)) {
        return true;
      }
    }
    return false;
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
      if (Stopped()) {
        return;
      }

      if (fdecl->hasBody()) {
        KnownExprs known_exprs{provenance.z3_ctx};
        if (visitor.Visit(fdecl->getBody(), known_exprs)) {
          changed = true;
          return;
        }
      }
    }
  }
}
}  // namespace rellic