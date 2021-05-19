/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "Util.h"

std::unique_ptr<clang::ASTUnit> GetASTUnit(const char *code) {
  auto unit{clang::tooling::buildASTFromCode(code, "out.c")};
  REQUIRE(unit != nullptr);
  return unit;
}