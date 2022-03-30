/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <string>

#include "rellic/BC/Compat/IntrinsicInst.h"

namespace llvm {
class Module;
class Type;
class Value;
class LLVMContext;
class GlobalObject;
}  // namespace llvm

namespace rellic {
// Serialize an LLVM object into a string.
std::string LLVMThingToString(llvm::Value *thing);
std::string LLVMThingToString(llvm::Type *thing);
std::string LLVMThingToString(llvm::DIType *thing);

// Try to verify a module.
bool VerifyModule(llvm::Module *module);

// Parses and loads a bitcode file into memory.
llvm::Module *LoadModuleFromFile(llvm::LLVMContext *context,
                                 std::string file_name,
                                 bool allow_failure = false);
llvm::Module *LoadModuleFromMemory(llvm::LLVMContext *context,
                                   std::string file_data,
                                   bool allow_failure = false);

// Check if an intrinsic ID is an annotation
bool IsAnnotationIntrinsic(llvm::Intrinsic::ID id);

// check if a global object is llvm metadata
bool IsGlobalMetadata(const llvm::GlobalObject &go);

void CloneMetadataInto(
    llvm::Instruction *dst,
    const llvm::SmallVector<std::pair<unsigned, llvm::MDNode *>, 16u> &mds);

void CopyMetadataTo(llvm::Value *src, llvm::Value *dst);

void RemovePHINodes(llvm::Module &module);

void LowerSwitches(llvm::Module &module);

// ARM backends (and probably some others) have a tendency to generate functions
// that make use of arrays passed by value, e.g. by value array arguments and
// using `insertvalue` to create copies with different elements. These
// operations do not have a clear equivalent in C, and are better handled by
// patching the bitcode to work around their semantics.

// Converts `insertvalue` instructions into an equivalent `alloca`, `load` and
// `store` sequences.
void RemoveInsertValues(llvm::Module &module);

// Converts by value array arguments and wraps them into a struct, so that
// semantics are preserved in C
void ConvertArrayArguments(llvm::Module &module);
}  // namespace rellic