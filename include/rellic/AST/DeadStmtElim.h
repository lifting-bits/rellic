/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/StmtVisitor.h>

#include "rellic/AST/ASTPass.h"
#include "rellic/AST/IRToASTVisitor.h"

namespace rellic {

/*
 * This pass eliminates statements that have no effect
 */
class DeadStmtElim : public clang::StmtVisitor<DeadStmtElim>, public ASTPass {
 protected:
  void RunImpl(clang::Stmt *stmt) override;

 public:
  DeadStmtElim(StmtToIRMap &provenance, clang::ASTUnit &unit,
               Substitutions &substitutions);

  void VisitIfStmt(clang::IfStmt *ifstmt);
  void VisitCompoundStmt(clang::CompoundStmt *compound);
};

}  // namespace rellic