/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */
#include "rellic/Exception.h"

namespace rellic {
Exception::Exception(const std::string& what) : std::runtime_error(what) {}
Exception::Exception(const char* what) : std::runtime_error(what) {}
}  // namespace rellic