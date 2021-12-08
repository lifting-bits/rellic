/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/BC/Compat/Value.h"

#include "rellic/BC/Version.h"

namespace rellic {

void DeleteValue(llvm::Instruction *inst) {
#if LLVM_VERSION_NUMBER >= LLVM_VERSION(5, 0)
  inst->deleteValue();
#else
  delete inst;
#endif
}

}  // namespace rellic