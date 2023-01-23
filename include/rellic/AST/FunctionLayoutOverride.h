/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once
#include <clang/AST/Decl.h>
#include <clang/AST/Type.h>
#include <llvm/IR/Argument.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instruction.h>

namespace rellic {
struct DecompilationContext;

class FunctionLayoutOverride {
 protected:
  DecompilationContext& dec_ctx;

 public:
  FunctionLayoutOverride(DecompilationContext& dec_ctx);
  virtual ~FunctionLayoutOverride();

  virtual bool HasOverride(llvm::Function& func);

  virtual std::vector<clang::QualType> GetArguments(llvm::Function& func);
  virtual void BeginFunctionVisit(llvm::Function& func,
                                  clang::FunctionDecl* fdecl);
  virtual bool VisitInstruction(llvm::Instruction& insn,
                                clang::FunctionDecl* fdecl,
                                clang::ValueDecl*& vdecl);
};

class FunctionLayoutOverrideCombiner final : public FunctionLayoutOverride {
 private:
  std::vector<std::unique_ptr<FunctionLayoutOverride>> overrides;

 public:
  FunctionLayoutOverrideCombiner(DecompilationContext& dec_ctx);
  template <typename T, typename... TArgs>
  void AddOverride(TArgs&&... args) {
    overrides.push_back(
        std::make_unique<T>(dec_ctx, std::forward<TArgs>(args)...));
  }

  void AddOverride(std::unique_ptr<FunctionLayoutOverride> provider);

  bool HasOverride(llvm::Function& func) final;

  std::vector<clang::QualType> GetArguments(llvm::Function& func) final;
  void BeginFunctionVisit(llvm::Function& func,
                          clang::FunctionDecl* fdecl) final;
  bool VisitInstruction(llvm::Instruction& insn, clang::FunctionDecl* fdecl,
                        clang::ValueDecl*& vdecl) final;
};
}  // namespace rellic