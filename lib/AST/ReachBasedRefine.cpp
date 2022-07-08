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

#include "rellic/AST/Util.h"

namespace rellic {

ReachBasedRefine::ReachBasedRefine(Provenance &provenance, clang::ASTUnit &unit)
    : TransformVisitor<ReachBasedRefine>(provenance, unit) {}

void ReachBasedRefine::CreateIfElseStmts(IfStmtVec stmts) {
  // Else-if candidate IfStmts and their Z3 form
  // reaching conditions.
  IfStmtVec elifs;
  z3::expr_vector conds(provenance.z3_ctx);
  // Test that determines if a new IfStmts is not
  // reachable from the already gathered IfStmts.
  auto IsUnrechable = [this, &conds](z3::expr cond) {
    return Prove(provenance.z3_ctx, !(cond && z3::mk_or(conds)));
  };
  // Test to determine if we have enough candidate
  // IfStmts to form an else-if cascade.
  auto IsTautology = [this, &conds] {
    return Prove(provenance.z3_ctx,
                 z3::mk_or(conds) == provenance.z3_ctx.bool_val(true));
  };

  bool done_something{false};
  for (size_t i{0}; i < body.size() && !done_something; ++i) {
    auto if_stmt{clang::dyn_cast<clang::IfStmt>(body[i])};
    if (!if_stmt) {
      ResetChain();
      continue;
    }
    // Clear else-if IfStmts if we find a path among them.
    auto cond = provenance.z3_exprs[provenance.conds[stmt]];
    if (stmt->getElse() || !IsUnrechable(cond)) {
      conds = z3::expr_vector(provenance.z3_ctx);
      elifs.clear();
    }
    // Add the current if-statement to the else-if candidates.
    conds.push_back(cond);
    elifs.push_back(stmt);
  }

  // Check if we have enough statements to work with
  if (elifs.size() < 2) {
    return;
  }

  // Create the else-if cascade
  clang::IfStmt *sub = nullptr;
  for (auto stmt : llvm::make_range(elifs.rbegin(), elifs.rend())) {
    auto then = stmt->getThen();
    if (stmt == elifs.back()) {
      sub = ast.CreateIf(provenance.marker_expr, then);
      provenance.conds[sub] = provenance.conds[stmt];
      substitutions[stmt] = sub;
    } else if (stmt == elifs.front()) {
      std::vector<clang::Stmt *> thens({then});
      sub->setElse(ast.CreateCompoundStmt(thens));
      substitutions[stmt] = nullptr;
    } else {
      auto elif = ast.CreateIf(provenance.marker_expr, then);
      provenance.conds[elif] = provenance.conds[stmt];
      sub->setElse(elif);
      sub = elif;
      substitutions[stmt] = nullptr;
    }
  }
}

/*
`body` will look like this at this point:

  ...
  i - n    : ...
  i - n + 1: if(cond_1) { } else if(cond_2) { } ... else if(cond_n) { }
  i - n + 2: if(cond_2) { } else if(cond_3) { } ... else if(cond_n) { }
  ...
  i - 1    : if(cond_n-1) { } else { }
  i        : if(cond_n) { }
  ...

but since we chained all of the statements into the first, we want to remove
the others from the body:

  ...
  i - n    : ...
  i - n + 1: if(cond_1) { } else if(cond_2) { } else if ...
  ...
*/
size_t start_delete{i - (ifs.size() - 2)};
size_t end_delete{i};
body.erase(body.erase(std::next(body.begin(), start_delete),
                      std::next(body.begin(), end_delete)));
done_something = true;
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