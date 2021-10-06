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
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/Value.h>
#include <rellic/AST/Util.h>

#include <memory>
#include <unordered_map>

namespace rellic {

using IRToNameMap = std::unordered_map<llvm::Value *, std::string>;

class DebugInfoVisitor : public llvm::InstVisitor<DebugInfoVisitor> {
 private:
  IRToNameMap names;

 public:
  IRToNameMap &GetIRToNameMap() { return names; }

  void visitDbgDeclareInst(llvm::DbgDeclareInst &inst);
};

}  // namespace rellic