/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include "rellic/AST/ASTPass.h"

namespace rellic {

/*
 * This pass propagates the condition of a while or do-while statement to all
 * subsequent if, while and do-while statements in the same lexical scope.
 *
 *   while(a) {
 *     foo();
 *   }
 *   if(a) {
 *     bar();
 *   }
 *
 * turns into
 *
 *   while(a) {
 *     foo();
 *   }
 *   if(1U) {
 *     bar();
 *   }
 *
 * It also propagates the conditions of if and while statements to their nested
 * statements.
 *
 *   if(a) {
 *     if(a && b) {
 *       foo();
 *     }
 *   }
 *
 * turns into
 *
 *   if(a) {
 *     if(1U && b) {
 *       foo();
 *     }
 *   }
 */
class NestedCondProp : public ASTPass {
 protected:
  void RunImpl() override;

 public:
  NestedCondProp(Provenance &provenance, clang::ASTUnit &unit);
};

}  // namespace rellic
