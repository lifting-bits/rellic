/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <llvm/IR/Module.h>
#include <llvm/Pass.h>

#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/TransformVisitor.h"

namespace rellic {

class NormalizeCond : public llvm::ModulePass,
                      public TransformVisitor<NormalizeCond> {
 private:
  clang::ASTUnit &unit;

 public:
  static char ID;

  NormalizeCond(StmtToIRMap &provenance, clang::ASTUnit &unit);

  bool VisitUnaryOperator(clang::UnaryOperator *op);
  bool VisitBinaryOperator(clang::BinaryOperator *op);

  bool runOnModule(llvm::Module &module) override;
};

}  // namespace rellic
