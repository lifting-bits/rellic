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
struct KnownExprs {
  std::unordered_map<z3::expr, bool> values;

  bool IsConstant(z3::expr expr) {
    if (Prove(expr)) {
      return true;
    }

    if (Prove(!expr)) {
      return true;
    }

    return false;
  }

  void AddExpr(z3::expr expr, bool value) {
    switch (expr.decl().decl_kind()) {
      case Z3_OP_TRUE:
      case Z3_OP_FALSE:
        return;
      default:
        break;
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

    values[expr] = value;
  }

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

    auto inner{known_exprs};
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

    auto inner{known_exprs};
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

NestedCondProp::NestedCondProp(Provenance& provenance, clang::ASTUnit& unit)
    : ASTPass(provenance, unit) {}

void NestedCondProp::RunImpl() {
  LOG(INFO) << "Propagating conditions";
  changed = false;
  ASTBuilder ast{ast_unit};
  CompoundVisitor visitor{provenance, ast, ast_ctx};

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