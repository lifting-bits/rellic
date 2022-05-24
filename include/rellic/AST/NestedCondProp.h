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
 * This pass propagates the condition of an outer if or while statement into the
 * conditions of its inner if statements. For example,
 *
 *   if(cond_a) {
 *     if(cond_a && cond_b) {
 *       body;
 *     }
 *   }
 *
 * turns into
 *
 *   if(cond_a) {
 *     if(1U && cond_b) {
 *       body;
 *     }
 *   }
 */
class NestedCondProp : public TransformVisitor<NestedCondProp> {
 private:
  std::unordered_map<clang::Stmt *, clang::Expr *> parent_conds;

 protected:
  void RunImpl() override;

 public:
  bool shouldTraversePostOrder() override { return false; }

  NestedCondProp(Provenance &provenance, clang::ASTUnit &unit);

  bool VisitIfStmt(clang::IfStmt *stmt);
  bool VisitWhileStmt(clang::WhileStmt *stmt);
};

}  // namespace rellic
