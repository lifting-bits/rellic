/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/ReachBasedRefine.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

namespace rellic {

ReachBasedRefine::ReachBasedRefine(Provenance &provenance, clang::ASTUnit &unit)
    : TransformVisitor<ReachBasedRefine>(provenance, unit),
      z3_ctx(new z3::context()),
      z3_gen(new rellic::Z3ConvVisitor(unit, z3_ctx.get())) {}

z3::expr ReachBasedRefine::GetZ3Cond(clang::IfStmt *ifstmt) {
  auto cond = ifstmt->getCond();
  auto expr = z3_gen->Z3BoolCast(z3_gen->GetOrCreateZ3Expr(cond));
  return expr.simplify();
}

bool ReachBasedRefine::VisitCompoundStmt(clang::CompoundStmt *compound) {
  std::vector<clang::Stmt *> body{compound->body_begin(), compound->body_end()};
  std::vector<clang::IfStmt *> ifs;
  z3::expr_vector conds{*z3_ctx};
  bool done_something{false};
  for (size_t i{0}; i < body.size(); ++i) {
    if (auto if_stmt = clang::dyn_cast<clang::IfStmt>(body[i])) {
      ifs.push_back(if_stmt);
      auto cond{GetZ3Cond(if_stmt)};
      if (!if_stmt->getElse() && Prove(*z3_ctx, !(cond && z3::mk_or(conds)))) {
        conds.push_back(GetZ3Cond(if_stmt));

        if (Prove(*z3_ctx, z3::mk_or(conds)) && ifs.size() > 2) {
          auto last_if{ifs[0]};
          for (auto stmt : ifs) {
            if (stmt == ifs.front()) {
              continue;
            }
            if (stmt == ifs.back()) {
              last_if->setElse(stmt->getThen());
            } else {
              last_if->setElse(stmt);
              last_if = stmt;
            }
          }

          size_t start_delete{i - (ifs.size() - 2)};
          size_t end_delete{i};
          body.erase(body.erase(std::next(body.begin(), start_delete),
                                std::next(body.begin(), end_delete)));
          done_something = true;
          break;
        }
      }
    }
    ifs.clear();
    conds.resize(0);
  }

  if (done_something) {
    substitutions[compound] = ast.CreateCompoundStmt(body);
  }
  return !Stopped();
}

void ReachBasedRefine::RunImpl() {
  LOG(INFO) << "Reachability-based refinement";
  TransformVisitor<ReachBasedRefine>::RunImpl();
  TraverseDecl(ast_ctx.getTranslationUnitDecl());
}

}  // namespace rellic