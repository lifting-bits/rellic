/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/RecursiveASTVisitor.h>

#include "rellic/AST/ASTPass.h"

namespace rellic {

/*
 * This pass combines the bodies of trivially true if statements and compound
 * statements into the body of their parent's body, and deletes trivially false
 * if statements. For example,
 *
 *   {
 *     if(1U) {
 *       body1;
 *     }
 *     if(0U) {
 *       body2;
 *     }
 *   }
 *
 * becomes
 *
 *   body1;
 */
class NestedScopeCombine
    : public clang::RecursiveASTVisitor<NestedScopeCombine>,
      public ASTPass {
 protected:
  void RunImpl(clang::Stmt *stmt) override;

 public:
  NestedScopeCombine(StmtToIRMap &provenance, clang::ASTUnit &unit,
                     Substitutions &substitutions);

  bool VisitIfStmt(clang::IfStmt *ifstmt);
  bool VisitCompoundStmt(clang::CompoundStmt *compound);
};

}  // namespace rellic
