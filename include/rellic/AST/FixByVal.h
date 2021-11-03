/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/Module.h>
#include <llvm/Pass.h>

#include <unordered_map>

namespace rellic {

class FixByVal : public llvm::ModulePass, public llvm::InstVisitor<FixByVal> {
 private:
  std::unordered_map<llvm::Function *, llvm::Function *> subst;

 public:
  static char ID;

  FixByVal();

  void visitFunction(llvm::Function &func);
  void visitCallInst(llvm::CallInst &call);
  bool runOnModule(llvm::Module &module) override;
};

}  // namespace rellic