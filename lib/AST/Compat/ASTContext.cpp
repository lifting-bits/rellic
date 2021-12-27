/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/Compat/ASTContext.h"

#include "rellic/BC/Version.h"

namespace rellic {

clang::QualType GetConstantArrayType(clang::ASTContext &ast_ctx,
                                     clang::QualType elm_type,
                                     const uint64_t arr_size) {
#if LLVM_VERSION_NUMBER >= LLVM_VERSION(10, 0)
  return ast_ctx.getConstantArrayType(
      elm_type, llvm::APInt(64, arr_size), nullptr,
      clang::ArrayType::ArraySizeModifier::Normal, 0);
#else
  return ast_ctx.getConstantArrayType(
      elm_type, llvm::APInt(64, arr_size),
      clang::ArrayType::ArraySizeModifier::Normal, 0);
#endif
}

clang::QualType GetRealTypeForBitwidth(clang::ASTContext &ast_ctx,
                                       unsigned dest_width) {
#if LLVM_VERSION_NUMBER >= LLVM_VERSION(11, 0)
  return ast_ctx.getRealTypeForBitwidth(dest_width, /*ExplicitIEEE=*/false);
#else
  return ast_ctx.getRealTypeForBitwidth(dest_width);
#endif
}

}  // namespace rellic
