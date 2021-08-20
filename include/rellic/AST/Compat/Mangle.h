/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

namespace llvm {
class raw_ostream;
}

namespace clang {
class MangleContext;
class CXXConstructorDecl;
class CXXDestructorDecl;
}  // namespace clang

namespace rellic {

void MangleNameCXXCtor(clang::MangleContext *ctx,
                       clang::CXXConstructorDecl *decl, llvm::raw_ostream &os);
void MangleNameCXXDtor(clang::MangleContext *ctx,
                       clang::CXXDestructorDecl *decl, llvm::raw_ostream &os);

}  // namespace rellic