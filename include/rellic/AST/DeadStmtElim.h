/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include "rellic/AST/ASTPass.h"
#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/TransformVisitor.h"

namespace rellic {

/*
 * This pass eliminates statements that have no effect
 */
class DeadStmtElim : public TransformVisitor<DeadStmtElim> {
 protected:
  void RunImpl() override;

 public:
  DeadStmtElim(StmtToIRMap &provenance, clang::ASTUnit &unit);

  bool VisitIfStmt(clang::IfStmt *ifstmt);
  bool VisitCompoundStmt(clang::CompoundStmt *compound);
};

}  // namespace rellic