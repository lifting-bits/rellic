/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <llvm/IR/Type.h>

#if LLVM_VERSION_NUMBER < LLVM_VERSION(11, 0)
using llvm::Type::FixedVectorTyID = llvm::Type::VectorTyID;
#endif