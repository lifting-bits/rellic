/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/Compat/Mangle.h"

#include <clang/AST/GlobalDecl.h>
#include <clang/AST/Mangle.h>

#include "rellic/BC/Version.h"

namespace rellic {

void MangleNameCXXCtor(clang::MangleContext *ctx,
                       clang::CXXConstructorDecl *decl, llvm::raw_ostream &os) {
#if LLVM_VERSION_NUMBER >= LLVM_VERSION(11, 0)
  ctx->mangleName(clang::GlobalDecl(decl), os);
#else
  ctx->mangleCXXCtor(decl, clang::Ctor_Complete, os);
#endif
}

void MangleNameCXXDtor(clang::MangleContext *ctx,
                       clang::CXXDestructorDecl *decl, llvm::raw_ostream &os) {
#if LLVM_VERSION_NUMBER >= LLVM_VERSION(11, 0)
  ctx->mangleName(clang::GlobalDecl(decl), os);
#else
  ctx->mangleCXXDtor(decl, clang::Dtor_Complete, os);
#endif
}
}  // namespace rellic