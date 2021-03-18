/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <memory>

#include <llvm/IRReader/IRReader.h>

#include "rellic/BC/Version.h"

#if LLVM_VERSION_NUMBER < LLVM_VERSION(3, 6)

namespace llvm {
class Module;

template <typename ...Args>
inline static std::unique_ptr<Module> parseIRFile(Args&... args) {
  return std::unique_ptr<Module>(ParseIRFile(args...));
}

}  // namespace llvm

#endif
