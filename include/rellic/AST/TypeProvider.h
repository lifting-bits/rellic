/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>

#include "rellic/AST/ASTBuilder.h"

namespace rellic {
struct DecompilationContext;

class TypeProvider {
 protected:
  DecompilationContext& dec_ctx;

 public:
  TypeProvider(DecompilationContext& dec_ctx);
  virtual ~TypeProvider();

  // Returns the return type of a function if available
  virtual clang::QualType GetFunctionReturnType(llvm::Function& func);

  // Returns the type of the argument if available
  virtual clang::QualType GetArgumentType(llvm::Argument& arg);

  // Returns the type of a global variable if available
  virtual clang::QualType GetGlobalVarType(llvm::GlobalVariable& gvar);
};

class TypeProviderCombiner : public TypeProvider {
 private:
  std::vector<std::unique_ptr<TypeProvider>> providers;

 public:
  TypeProviderCombiner(DecompilationContext& dec_ctx);
  template <typename T>
  void AddProvider() {
    providers.push_back(std::make_unique<T>(dec_ctx));
  }

  void AddProvider(std::unique_ptr<TypeProvider> provider);

  clang::QualType GetFunctionReturnType(llvm::Function& func) override;
  clang::QualType GetArgumentType(llvm::Argument& arg) override;
  clang::QualType GetGlobalVarType(llvm::GlobalVariable& gvar) override;
};
}  // namespace rellic