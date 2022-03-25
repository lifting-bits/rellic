/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include "rellic/AST/TransformVisitor.h"
#include "rellic/AST/Z3ConvVisitor.h"

namespace rellic {

/*
 * This pass restructures a sequence of if statements that have a shape like
 *
 *   if(cond1 && !cond2 && !cond3) {
 *     body1;
 *   }
 *   if(cond2 && !cond1 && !cond3) {
 *     body2;
 *   }
 *   if(cond3 && !cond1 && !cond2) {
 *     body3;
 *   }
 *
 * into
 *
 *   if(cond1) {
 *     body1;
 *   } else if(cond2) {
 *     body2;
 *   } else if(cond3) {
 *     body3;
 *   }
 */
class ReachBasedRefine : public TransformVisitor<ReachBasedRefine> {
 private:
  std::unique_ptr<z3::context> z3_ctx;
  std::unique_ptr<rellic::Z3ConvVisitor> z3_gen;

  z3::tactic z3_solver;

  z3::expr GetZ3Cond(clang::IfStmt *ifstmt);

  bool Prove(z3::expr expr);

  using IfStmtVec = std::vector<clang::IfStmt *>;

  void CreateIfElseStmts(IfStmtVec stmts);

 protected:
  void RunImpl() override;

 public:
  ReachBasedRefine(StmtToIRMap &provenance, ExprToUseMap &use_provenance,
                   clang::ASTUnit &unit);

  bool VisitCompoundStmt(clang::CompoundStmt *compound);
};

}  // namespace rellic