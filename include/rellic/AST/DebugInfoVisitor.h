/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/Stmt.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <rellic/AST/Util.h>

#include <memory>
#include <unordered_map>

namespace rellic {

using IRToNameMap = std::unordered_map<llvm::Value *, std::string>;
using IRToScopeMap = std::unordered_map<llvm::Value *, llvm::DILocalScope *>;
using IRToDITypeMap = std::unordered_map<llvm::Value *, llvm::DIType *>;
using IRTypeToDITypeMap = std::unordered_map<llvm::Type *, llvm::DIType *>;

class DebugInfoVisitor : public llvm::InstVisitor<DebugInfoVisitor> {
 private:
  IRToNameMap names;
  IRToScopeMap scopes;
  IRToDITypeMap valtypes;
  IRTypeToDITypeMap types;

  void walkType(llvm::Type *type, llvm::DIType *ditype);

 public:
  IRToNameMap &GetIRToNameMap() { return names; }
  IRToScopeMap &GetIRToScopeMap() { return scopes; }
  IRToDITypeMap &GetIRToDITypeMap() { return valtypes; }
  IRTypeToDITypeMap &GetIRTypeToDITypeMap() { return types; }

  void visitDbgDeclareInst(llvm::DbgDeclareInst &inst);
  void visitInstruction(llvm::Instruction &inst);

  void visitFunction(llvm::Function &func);
};

}  // namespace rellic