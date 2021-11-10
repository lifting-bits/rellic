/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/InstVisitor.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace rellic {

using IRToNameMap = std::unordered_map<llvm::Value *, std::string>;
using IRToScopeMap = std::unordered_map<llvm::Value *, llvm::DILocalScope *>;
using IRToDITypeMap = std::unordered_map<llvm::Value *, llvm::DIType *>;
using IRTypeToDITypeMap = std::unordered_map<llvm::Type *, llvm::DIType *>;
using IRFuncToDITypeMap =
    std::unordered_map<llvm::Function *, llvm::DISubroutineType *>;
using IRArgToDITypeMap = std::unordered_map<llvm::Argument *, llvm::DIType *>;

class DebugInfoCollector : public llvm::InstVisitor<DebugInfoCollector> {
 private:
  IRToNameMap names;
  IRToScopeMap scopes;
  IRToDITypeMap valtypes;
  IRTypeToDITypeMap types;
  IRFuncToDITypeMap funcs;
  IRArgToDITypeMap args;
  std::vector<llvm::DICompositeType *> structs;

  void WalkType(llvm::Type *type, llvm::DIType *ditype);

 public:
  IRToNameMap &GetIRToNameMap() { return names; }
  IRToScopeMap &GetIRToScopeMap() { return scopes; }
  IRToDITypeMap &GetIRToDITypeMap() { return valtypes; }
  IRTypeToDITypeMap &GetIRTypeToDITypeMap() { return types; }
  IRFuncToDITypeMap &GetIRFuncToDITypeMap() { return funcs; }
  IRArgToDITypeMap &GetIRArgToDITypeMap() { return args; }
  std::vector<llvm::DICompositeType *> &GetStructs() { return structs; }

  void visitDbgDeclareInst(llvm::DbgDeclareInst &inst);
  void visitInstruction(llvm::Instruction &inst);

  void visitFunction(llvm::Function &func);
};

}  // namespace rellic