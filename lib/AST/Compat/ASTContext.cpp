/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/Compat/ASTContext.h"

#include <clang/Basic/TargetInfo.h>

#include "rellic/BC/Version.h"

namespace rellic {

clang::QualType GetConstantArrayType(clang::ASTContext &ast_ctx,
                                     clang::QualType elm_type,
                                     const uint64_t arr_size) {
  return ast_ctx.getConstantArrayType(
      elm_type, llvm::APInt(64, arr_size), nullptr,
      clang::ArrayType::ArraySizeModifier::Normal, 0);
}

clang::QualType GetRealTypeForBitwidth(clang::ASTContext &ast_ctx,
                                       unsigned dest_width) {
  return ast_ctx.getRealTypeForBitwidth(dest_width,
                                        clang::FloatModeKind::Float);
}

}  // namespace rellic
