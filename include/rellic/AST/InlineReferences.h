/*
 * Copyright (c) 2023-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <unordered_map>
#include <unordered_set>

#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/TransformVisitor.h"

namespace rellic {

/*
 * This pass removes references to variables that can be inlined
 *
 *   int x = 3 + y;
 *   if(x) { ... }
 * becomes
 *   if(3 + y) { ... }
 */
class InlineReferences : public TransformVisitor<InlineReferences> {
 private:
  std::unordered_map<llvm::Value*, unsigned> refs;
  std::unordered_set<clang::ValueDecl*> removable_decls;

 protected:
  void RunImpl() override;

 public:
  InlineReferences(DecompilationContext& dec_ctx);

  bool VisitCompoundStmt(clang::CompoundStmt* stmt);
};

}  // namespace rellic
