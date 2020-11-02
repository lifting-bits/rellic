/*
 * Copyright (c) 2020 Trail of Bits, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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