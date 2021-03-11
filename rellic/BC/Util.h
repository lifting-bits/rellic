/*
 * Copyright (c) 2019 Trail of Bits, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <string>

namespace llvm {
class Module;
class Type;
class Value;
class LLVMContext;
}  // namespace llvm

namespace rellic {
// Serialize an LLVM object into a string.
std::string LLVMThingToString(llvm::Value *thing);
std::string LLVMThingToString(llvm::Type *thing);

// Try to verify a module.
bool VerifyModule(llvm::Module *module);

// Parses and loads a bitcode file into memory.
llvm::Module *LoadModuleFromFile(llvm::LLVMContext *context,
                                 std::string file_name,
                                 bool allow_failure = false);

bool IsAnnotationIntrinsic(llvm::Intrinsic::ID id);

}  // namespace rellic