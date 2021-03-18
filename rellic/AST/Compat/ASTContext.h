/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/ASTContext.h>

namespace rellic {

clang::QualType GetConstantArrayType(clang::ASTContext &ast_ctx,
                                     clang::QualType elm_type,
                                     const uint64_t arr_size);

clang::QualType GetRealTypeForBitwidth(clang::ASTContext &ast_ctx,
                                       unsigned dest_width);

}  // namespace rellic