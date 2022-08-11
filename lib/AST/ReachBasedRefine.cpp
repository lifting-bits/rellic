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
    : TransformVisitor<ReachBasedRefine>(provenance, unit) {}

bool ReachBasedRefine::VisitCompoundStmt(clang::CompoundStmt *compound) {
  std::vector<clang::Stmt *> body{compound->body_begin(), compound->body_end()};
  std::vector<clang::IfStmt *> ifs;
  z3::expr_vector conds{provenance.z3_ctx};
  bool done_something{false};
  for (size_t i{0}; i < body.size(); ++i) {
    if (auto if_stmt = clang::dyn_cast<clang::IfStmt>(body[i])) {
      ifs.push_back(if_stmt);
      auto cond{provenance.z3_exprs[provenance.conds[if_stmt]]};
      if (!if_stmt->getElse() && Prove(!(cond && z3::mk_or(conds)))) {
        conds.push_back(cond);

        if (Prove(z3::mk_or(conds)) && ifs.size() > 2) {
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