/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include "rellic/AST/TransformVisitor.h"

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
class NestedScopeCombine : public TransformVisitor<NestedScopeCombine> {
 protected:
  void RunImpl() override;

 public:
  NestedScopeCombine(StmtToIRMap &provenance, clang::ASTUnit &unit);

  bool VisitIfStmt(clang::IfStmt *ifstmt);
  bool VisitCompoundStmt(clang::CompoundStmt *compound);
};

}  // namespace rellic
