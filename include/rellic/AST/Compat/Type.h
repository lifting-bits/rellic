/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/Type.h>

#include "rellic/BC/Version.h"

namespace rellic {
#if LLVM_VERSION_NUMBER >= LLVM_VERSION(8, 0)
constexpr auto attr_nonnull = clang::attr::TypeNonNull;
#else
constexpr auto attr_nonnull = clang::AttributedType::attr_nonnull;
#endif
}  // namespace rellic