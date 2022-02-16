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
 * This pass transforms unbounded loops with a break in their body into loops
 * with a condition. For example,
 *
 *   while(1U) {
 *     if(cond) {
 *       break;
 *     }
 *     body;
 *   }
 *
 * becomes
 *
 *   while(!cond) {
 *     body;
 *   }
 */
class LoopRefine : public TransformVisitor<LoopRefine> {
 protected:
  void RunImpl() override;

 public:
  LoopRefine(StmtToIRMap &provenance, clang::ASTUnit &unit);

  bool VisitWhileStmt(clang::WhileStmt *loop);
};

}  // namespace rellic
