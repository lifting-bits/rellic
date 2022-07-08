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
    : public clang::StmtVisitor<CompoundVisitor, bool, z3::expr&> {
 private:
  Provenance& provenance;
  ASTBuilder& ast;
  clang::ASTContext& ctx;

  bool IsConstant(z3::expr expr) {
    if (Prove(provenance.z3_ctx, expr)) {
      return false;
    }

    if (Prove(provenance.z3_ctx, !expr)) {
      return false;
    }

    return true;
  }

  z3::expr Simplify(z3::expr expr) {
    return HeavySimplify(provenance.z3_ctx, expr);
  }

 public:
  CompoundVisitor(Provenance& provenance, ASTBuilder& ast,
                  clang::ASTContext& ctx)
      : provenance(provenance), ast(ast), ctx(ctx) {}

  bool VisitCompoundStmt(clang::CompoundStmt* compound, z3::expr& true_exprs) {
    bool changed{false};
    for (auto stmt : compound->body()) {
      changed |= Visit(stmt, true_exprs);
    }

    return changed;
  }

  bool VisitWhileStmt(clang::WhileStmt* while_stmt, z3::expr& true_exprs) {
    bool changed{false};
    if (while_stmt->getCond() == provenance.marker_expr) {
      auto old_cond{provenance.z3_exprs[provenance.conds[while_stmt]]};
      auto new_cond{Simplify(old_cond && true_exprs)};
      LOG(INFO) << "known: " << true_exprs.to_string()
                << " old: " << old_cond.to_string()
                << " new: " << new_cond.to_string();
      if (!z3::eq(old_cond, new_cond)) {
        provenance.conds[while_stmt] = provenance.z3_exprs.size();
        provenance.z3_exprs.push_back(new_cond);
        changed = true;
      }
    }

    auto cond{provenance.z3_exprs[provenance.conds[while_stmt]]};
    z3::expr inner{true_exprs};
    bool isConstant{IsConstant(cond)};
    if (!isConstant) {
      inner = Simplify(inner && cond);
    }
    changed |= Visit(while_stmt->getBody(), inner);

    if (!isConstant) {
      true_exprs = Simplify(true_exprs && !cond);
    }
    return changed;
  }

  bool VisitDoStmt(clang::DoStmt* do_stmt, z3::expr& true_exprs) {
    bool changed{false};
    if (do_stmt->getCond() == provenance.marker_expr) {
      auto old_cond{provenance.z3_exprs[provenance.conds[do_stmt]]};
      auto new_cond{Simplify(old_cond && true_exprs)};
      LOG(INFO) << "known: " << true_exprs.to_string()
                << " old: " << old_cond.to_string()
                << " new: " << new_cond.to_string();
      if (!z3::eq(old_cond, new_cond)) {
        provenance.conds[do_stmt] = provenance.z3_exprs.size();
        provenance.z3_exprs.push_back(new_cond);
        changed = true;
      }
    }

    auto cond{provenance.z3_exprs[provenance.conds[do_stmt]]};
    auto inner{true_exprs};
    Visit(do_stmt->getBody(), inner);

    if (!IsConstant(cond)) {
      true_exprs = Simplify(true_exprs && !cond);
    }
    return changed;
  }

  bool VisitIfStmt(clang::IfStmt* if_stmt, z3::expr& true_exprs) {
    bool changed{false};
    if (if_stmt->getCond() == provenance.marker_expr) {
      auto old_cond{provenance.z3_exprs[provenance.conds[if_stmt]]};
      auto new_cond{Simplify(old_cond && true_exprs)};
      LOG(INFO) << "known: " << true_exprs.to_string()
                << " old: " << old_cond.to_string()
                << " new: " << new_cond.to_string();
      if (!z3::eq(old_cond, new_cond)) {
        provenance.conds[if_stmt] = provenance.z3_exprs.size();
        provenance.z3_exprs.push_back(new_cond);
        changed = true;
      }
    }

    auto cond{provenance.z3_exprs[provenance.conds[if_stmt]]};
    auto inner_then{true_exprs};
    bool isConstant{IsConstant(cond)};
    if (isConstant) {
      inner_then = Simplify(inner_then && cond);
    }
    Visit(if_stmt->getThen(), inner_then);

    if (if_stmt->getElse()) {
      auto inner_else{true_exprs};
      if (!isConstant) {
        inner_else = Simplify(inner_else && !cond);
      }
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
        auto true_exprs{provenance.z3_ctx.bool_val(true)};
        changed |= visitor.Visit(fdecl->getBody(), true_exprs);
      }
    }
  }
}
}  // namespace rellic