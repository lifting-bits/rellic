/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/VariableProvider.h"

#include <clang/AST/Type.h>

#include "rellic/AST/Util.h"

namespace rellic {
VariableProvider::VariableProvider(DecompilationContext& dec_ctx)
    : dec_ctx(dec_ctx) {}
VariableProvider::~VariableProvider() = default;

clang::QualType VariableProvider::ArgumentAsLocal(llvm::Argument&) {
  return {};
}

VariableProviderCombiner::VariableProviderCombiner(
    DecompilationContext& dec_ctx)
    : VariableProvider(dec_ctx) {}

void VariableProviderCombiner::AddProvider(
    std::unique_ptr<VariableProvider> provider) {
  providers.push_back(std::move(provider));
}

clang::QualType VariableProviderCombiner::ArgumentAsLocal(llvm::Argument& arg) {
  for (auto it{providers.rbegin()}; it != providers.rend(); ++it) {
    auto& provider{*it};
    auto res{provider->ArgumentAsLocal(arg)};
    if (!res.isNull()) {
      return res;
    }
  }
  return {};
}
}  // namespace rellic