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

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <llvm/ADT/SmallVector.h>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>

#include "rellic/BC/Compat/Error.h"
#include "rellic/BC/Compat/IRReader.h"
#include "rellic/BC/Compat/Verifier.h"
#include "rellic/BC/Util.h"

namespace rellic {

namespace {

// Convert an LLVM thing (e.g. `llvm::Value` or `llvm::Type`) into
// a `std::string`.
template <typename T>
inline static std::string DoLLVMThingToString(T *thing) {
  if (thing) {
    std::string str;
    llvm::raw_string_ostream str_stream(str);
    thing->print(str_stream);
    return str;
  } else {
    return "(null)";
  }
}

}  // namespace

std::string LLVMThingToString(llvm::Value *thing) {
  return DoLLVMThingToString(thing);
}

std::string LLVMThingToString(llvm::Type *thing) {
  return DoLLVMThingToString(thing);
}

// Try to verify a module.
bool VerifyModule(llvm::Module *module) {
  std::string error;
  llvm::raw_string_ostream error_stream(error);
  if (llvm::verifyModule(*module, &error_stream)) {
    error_stream.flush();
    LOG(ERROR) << "Error verifying module read from file: " << error;
    return false;
  } else {
    return true;
  }
}

// Reads an LLVM module from a file.
llvm::Module *LoadModuleFromFile(llvm::LLVMContext *context,
                                 std::string file_name, bool allow_failure) {
  llvm::SMDiagnostic err;
  auto mod_ptr = llvm::parseIRFile(file_name, err, *context);
  auto module = mod_ptr.release();

  if (!module) {
    LOG_IF(FATAL, !allow_failure) << "Unable to parse module file " << file_name
                                  << ": " << err.getMessage().str();
    return nullptr;
  }

  auto ec = module->materializeAll();  // Just in case.
  if (ec) {
    LOG_IF(FATAL, !allow_failure)
        << "Unable to materialize everything from " << file_name;
    delete module;
    return nullptr;
  }

  if (!VerifyModule(module)) {
    LOG_IF(FATAL, !allow_failure)
        << "Error verifying module read from file " << file_name;
    delete module;
    return nullptr;
  }

  return module;
}

bool IsAnnotationIntrinsic(llvm::Intrinsic::ID id) {
  // this is a copy of IntrinsicInst::isAssumeLikeIntrinsic in LLVM12+
  // NOTE(artem): This probalby needs some compat wrappers for older LLVM
  switch (id) {
    case llvm::Intrinsic::assume:
    case llvm::Intrinsic::sideeffect:
    // case llvm::Intrinsic::pseudoprobe:
    case llvm::Intrinsic::dbg_declare:
    case llvm::Intrinsic::dbg_value:
    case llvm::Intrinsic::dbg_label:
    case llvm::Intrinsic::invariant_start:
    case llvm::Intrinsic::invariant_end:
    case llvm::Intrinsic::lifetime_start:
    case llvm::Intrinsic::lifetime_end:
    // case llvm::Intrinsic::experimental_noalias_scope_decl:
    case llvm::Intrinsic::objectsize:
    case llvm::Intrinsic::ptr_annotation:
    case llvm::Intrinsic::var_annotation:
      return false;
  }
  return true;
}

}  // namespace rellic