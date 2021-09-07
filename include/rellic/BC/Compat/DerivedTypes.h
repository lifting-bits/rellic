/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */
#pragma once

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/IR/DerivedTypes.h>

#include "rellic/BC/Version.h"

namespace rellic {

unsigned GetNumElements(llvm::VectorType *type) {
#if LLVM_VERSION_NUMBER >= LLVM_VERSION(12, 0)
  if (auto fixed_vec_ty = llvm::dyn_cast<llvm::FixedVectorType>(type)) {
    return fixed_vec_ty->getNumElements();
  }

  LOG(FATAL) << "Given VectorType is not a FixedVectorType.";

  return 0U;
#else
  return type->getNumElements();
#endif
}

}  // namespace rellic