/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <llvm/Config/llvm-config.h>
#include "rellic/BC/Version.h"

#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/Type.h>
#include <llvm/ADT/APInt.h>
#include <llvm/IR/Type.h>

namespace rellic {
namespace compat {

// LLVM 17+: getMinSignedBits() -> getSignificantBits()
inline unsigned GetSignificantBits(const llvm::APInt& val) {
#if LLVM_VERSION_NUMBER < LLVM_VERSION(17, 0)
  return val.getMinSignedBits();
#else
  return val.getSignificantBits();
#endif
}

inline unsigned GetSignificantBits(const llvm::APSInt& val) {
#if LLVM_VERSION_NUMBER < LLVM_VERSION(17, 0)
  return val.getMinSignedBits();
#else
  return val.getSignificantBits();
#endif
}

// LLVM 17+: getNullValue() -> getZero()
inline llvm::APInt GetZeroAPInt(unsigned width) {
#if LLVM_VERSION_NUMBER < LLVM_VERSION(17, 0)
  return llvm::APInt::getNullValue(width);
#else
  return llvm::APInt::getZero(width);
#endif
}

// LLVM 17+: AttributeCommonInfo constructor signature changed
inline clang::AttributeCommonInfo MakeAttributeInfo() {
#if LLVM_VERSION_NUMBER < LLVM_VERSION(17, 0)
  return clang::AttributeCommonInfo(clang::SourceLocation{});
#else
  return clang::AttributeCommonInfo(
      nullptr, clang::SourceLocation{},
      clang::AttributeCommonInfo::Form::GNU());
#endif
}

// LLVM 20+: getBitWidthValue() no longer takes ASTContext
inline unsigned GetFieldBitWidth(clang::FieldDecl* field,
                                 clang::ASTContext& ctx) {
#if LLVM_VERSION_NUMBER < LLVM_VERSION(20, 0)
  return field->getBitWidthValue(ctx);
#else
  (void)ctx;  // Unused in LLVM 20+
  return field->getBitWidthValue();
#endif
}

// LLVM 20+: Enum namespace flattening

#if LLVM_VERSION_NUMBER < LLVM_VERSION(20, 0)
constexpr auto CharacterKind_Ascii = clang::CharacterLiteral::CharacterKind::Ascii;
constexpr auto StringKind_Ordinary = clang::StringLiteral::StringKind::Ordinary;
constexpr auto TagKind_Struct = clang::TagTypeKind::TTK_Struct;
constexpr auto TagKind_Union = clang::TagTypeKind::TTK_Union;
constexpr auto ArraySizeMod_Normal = clang::ArrayType::ArraySizeModifier::Normal;
constexpr auto ElabTypeKW_None = clang::ElaboratedTypeKeyword::ETK_None;
constexpr auto VectorKind_Generic = clang::VectorType::GenericVector;
#else
constexpr auto CharacterKind_Ascii = clang::CharacterLiteralKind::Ascii;
constexpr auto StringKind_Ordinary = clang::StringLiteralKind::Ordinary;
constexpr auto TagKind_Struct = clang::TagTypeKind::Struct;
constexpr auto TagKind_Union = clang::TagTypeKind::Union;
constexpr auto ArraySizeMod_Normal = clang::ArraySizeModifier::Normal;
constexpr auto ElabTypeKW_None = clang::ElaboratedTypeKeyword::None;
constexpr auto VectorKind_Generic = clang::VectorKind::Generic;
#endif

// LLVM 20+: Optional -> std::optional
#if LLVM_VERSION_NUMBER < LLVM_VERSION(20, 0)
template <typename T>
using Optional = llvm::Optional<T>;
constexpr auto nullopt = llvm::None;
#else
#include <optional>
template <typename T>
using Optional = std::optional<T>;
constexpr auto nullopt = std::nullopt;
#endif

} // namespace compat
} // namespace rellic
