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

#include <unordered_map>
#include <vector>

#include "rellic/AST/ASTBuilder.h"
#include "rellic/AST/Util.h"

namespace std {
template <>
struct hash<z3::expr> {
  size_t operator()(const z3::expr& e) const { return e.id(); }
};

template <>
struct equal_to<z3::expr> {
  bool operator()(const z3::expr& a, const z3::expr& b) const {
    return a.id() == b.id();
  }
};
}  // namespace std

namespace rellic {
// Stores a set of expression that have a known value, so that they can be
// recognized as part of larger expressions and simplified.
struct KnownExprs {
  std::unordered_map<z3::expr, bool> values;

  void AddExpr(z3::expr expr, bool value) {
    // When adding expressions to the set of known values, it's important that
    // they are added in their smallest possible form. E.g., if it's known that
    // `A && B` is true, then both of its subexpressions are true, and we should
    // add those instead.
    // This is so that `A && B && C` can be used to simplify smaller
    // expressions, like `A && B`, which would otherwise not be recognized.
    switch (expr.decl().decl_kind()) {
      case Z3_OP_TRUE:
      case Z3_OP_FALSE:
        return;
      default:
        break;
    }

    // If !A has value V, then A has value !V, so add that instead.
    if (expr.is_not()) {
      AddExpr(expr.arg(0), !value);
      return;
    }

    // A unary && or ||, just add the single subexpression
    if (expr.num_args() == 1) {
      AddExpr(expr.arg(0), value);
      return;
    }

    // A true && expression means all of its subexpressions are true
    if (value && expr.is_and()) {
      for (auto e : expr.args()) {
        AddExpr(e, true);
      }
      return;
    }

    // A false || expression means all of its subexpressions are false
    if (!value && expr.is_or()) {
      for (auto e : expr.args()) {
        AddExpr(e, false);
      }
      return;
    }

    values[expr] = value;
  }

  // Simplify an expression `expr` using all the known values stored. Sets
  // `found` to true is any simplification has been applied.
  z3::expr ApplyAssumptions(z3::expr expr, bool& found) {
    if (values.empty()) {
      return expr;
    }

    if (values.find(expr) != values.end()) {
      found = true;
      return expr.ctx().bool_val(values[expr]);
    }

    if (expr.is_and() || expr.is_or()) {
      z3::expr_vector args{expr.ctx()};
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
  DecompilationContext& dec_ctx;
  ASTBuilder& ast;
  clang::ASTContext& ctx;

 public:
  CompoundVisitor(DecompilationContext& dec_ctx, ASTBuilder& ast,
                  clang::ASTContext& ctx)
      : dec_ctx(dec_ctx), ast(ast), ctx(ctx) {}

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
    auto cond_idx{dec_ctx.conds[while_stmt]};
    bool changed{false};
    auto old_cond{dec_ctx.z3_exprs[cond_idx]};
    auto new_cond{known_exprs.ApplyAssumptions(old_cond, changed)};
    if (while_stmt->getCond() != dec_ctx.marker_expr && changed) {
      dec_ctx.z3_exprs.set(cond_idx, new_cond);
      return true;
    }

    auto inner{known_exprs};
    inner.AddExpr(new_cond, true);
    known_exprs.AddExpr(new_cond, false);

    if (Visit(while_stmt->getBody(), inner)) {
      return true;
    }
    return false;
  }

  bool VisitDoStmt(clang::DoStmt* do_stmt, KnownExprs& known_exprs) {
    auto cond_idx{dec_ctx.conds[do_stmt]};
    bool changed{false};
    auto old_cond{dec_ctx.z3_exprs[cond_idx]};
    auto new_cond{known_exprs.ApplyAssumptions(old_cond, changed)};
    if (do_stmt->getCond() == dec_ctx.marker_expr && changed) {
      dec_ctx.z3_exprs.set(cond_idx, new_cond);
      return true;
    }

    auto inner{known_exprs};
    known_exprs.AddExpr(new_cond, false);

    if (Visit(do_stmt->getBody(), inner)) {
      return true;
    }

    return false;
  }

  bool VisitIfStmt(clang::IfStmt* if_stmt, KnownExprs& known_exprs) {
    auto cond_idx{dec_ctx.conds[if_stmt]};
    bool changed{false};
    auto old_cond{dec_ctx.z3_exprs[cond_idx]};
    auto new_cond{known_exprs.ApplyAssumptions(old_cond, changed)};
    if (if_stmt->getCond() == dec_ctx.marker_expr && changed) {
      dec_ctx.z3_exprs.set(cond_idx, new_cond);
      return true;
    }

    auto inner_then{known_exprs};
    inner_then.AddExpr(new_cond, true);
    if (Visit(if_stmt->getThen(), inner_then)) {
      return true;
    }

    if (if_stmt->getElse()) {
      auto inner_else{known_exprs};
      inner_else.AddExpr(new_cond, false);
      if (Visit(if_stmt->getElse(), inner_else)) {
        return true;
      }
    }
    return false;
  }
};

NestedCondProp::NestedCondProp(DecompilationContext& dec_ctx,
                               clang::ASTUnit& unit)
    : ASTPass(dec_ctx, unit) {}

void NestedCondProp::RunImpl() {
  LOG(INFO) << "Propagating conditions";
  changed = false;
  ASTBuilder ast{ast_unit};
  CompoundVisitor visitor{dec_ctx, ast, ast_ctx};

  for (auto decl : ast_ctx.getTranslationUnitDecl()->decls()) {
    if (auto fdecl = clang::dyn_cast<clang::FunctionDecl>(decl)) {
      if (Stopped()) {
        return;
      }

      if (fdecl->hasBody()) {
        KnownExprs known_exprs{};
        if (visitor.Visit(fdecl->getBody(), known_exprs)) {
          changed = true;
          return;
        }
      }
    }
  }
}
}  // namespace rellic