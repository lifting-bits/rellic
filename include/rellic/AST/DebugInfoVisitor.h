/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/Stmt.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/Value.h>
#include <rellic/AST/Util.h>

#include <memory>
#include <unordered_map>

namespace rellic {

using IRToNameMap = std::unordered_map<llvm::Value *, std::string>;
using IRToScopeMap = std::unordered_map<llvm::Value *, llvm::DILocalScope *>;

class DebugInfoVisitor : public llvm::InstVisitor<DebugInfoVisitor> {
 private:
  IRToNameMap names;
  IRToScopeMap scopes;

 public:
  IRToNameMap &GetIRToNameMap() { return names; }
  IRToScopeMap &GetIRToScopeMap() { return scopes; }

  void visitDbgDeclareInst(llvm::DbgDeclareInst &inst);
  void visitInstruction(llvm::Instruction &inst);
};

}  // namespace rellic