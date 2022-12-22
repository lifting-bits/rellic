/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once
#include <clang/AST/Decl.h>
#include <llvm/IR/Argument.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>

#include "rellic/AST/ASTBuilder.h"

namespace rellic {
struct DecompilationContext;

class VariableProvider {
 protected:
  DecompilationContext& dec_ctx;

 public:
  VariableProvider(DecompilationContext& dec_ctx);
  virtual ~VariableProvider();

  virtual clang::QualType ArgumentAsLocal(llvm::Argument& arg);
};

class VariableProviderCombiner final : public VariableProvider {
 private:
  std::vector<std::unique_ptr<VariableProvider>> providers;

 public:
  VariableProviderCombiner(DecompilationContext& dec_ctx);
  template <typename T, typename... TArgs>
  void AddProvider(TArgs&&... args) {
    providers.push_back(
        std::make_unique<T>(dec_ctx, std::forward<TArgs>(args)...));
  }

  void AddProvider(std::unique_ptr<VariableProvider> provider);

  clang::QualType ArgumentAsLocal(llvm::Argument& arg) final;
};
}  // namespace rellic