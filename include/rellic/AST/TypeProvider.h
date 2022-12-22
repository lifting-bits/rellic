/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>

#include "rellic/AST/ASTBuilder.h"

namespace rellic {
struct DecompilationContext;

class TypeProvider {
 protected:
  DecompilationContext& dec_ctx;

 public:
  TypeProvider(DecompilationContext& dec_ctx);
  virtual ~TypeProvider();

  // Returns the return type of a function if available.
  // A null return value is assumed to mean that no info is available.
  virtual clang::QualType GetFunctionReturnType(llvm::Function& func);

  // Returns the type of the argument if available.
  // A null return value is assumed to mean that no info is available.
  virtual clang::QualType GetArgumentType(llvm::Argument& arg);

  // Returns the type of a global variable if available.
  // A null return value is assumed to mean that no info is available.
  virtual clang::QualType GetGlobalVarType(llvm::GlobalVariable& gvar);

  // Returns the type of an alloca variable if available.
  // A null return value is assumed to mean that no info is available.
  virtual clang::QualType GetAllocaType(llvm::AllocaInst& alloca);
};

class TypeProviderCombiner final : public TypeProvider {
 private:
  std::vector<std::unique_ptr<TypeProvider>> providers;

 public:
  TypeProviderCombiner(DecompilationContext& dec_ctx);
  template <typename T, typename... TArgs>
  void AddProvider(TArgs&&... args) {
    providers.push_back(
        std::make_unique<T>(dec_ctx, std::forward<TArgs>(args)...));
  }

  void AddProvider(std::unique_ptr<TypeProvider> provider);

  clang::QualType GetFunctionReturnType(llvm::Function& func) final;
  clang::QualType GetArgumentType(llvm::Argument& arg) final;
  clang::QualType GetGlobalVarType(llvm::GlobalVariable& gvar) final;
  clang::QualType GetAllocaType(llvm::AllocaInst& alloca) final;
};
}  // namespace rellic