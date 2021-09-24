/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include "rellic/BC/Version.h"

#if LLVM_VERSION_NUMBER >= LLVM_VERSION(3, 5)
#include <llvm/IR/Verifier.h>
#else
#include <llvm/Analysis/Verifier.h>
#endif
