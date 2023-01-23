/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/FunctionLayoutOverride.h"

#include <clang/AST/Type.h>
#include <llvm/IR/Argument.h>

#include "rellic/AST/Util.h"

namespace rellic {
FunctionLayoutOverride::FunctionLayoutOverride(DecompilationContext &dec_ctx)
    : dec_ctx(dec_ctx) {}
FunctionLayoutOverride::~FunctionLayoutOverride() = default;

bool FunctionLayoutOverride::HasOverride(llvm::Function &func) { return false; }

std::vector<clang::QualType> FunctionLayoutOverride::GetArguments(
    llvm::Function &func) {
  return {};
}

void FunctionLayoutOverride::BeginFunctionVisit(llvm::Function &func,
                                                clang::FunctionDecl *fdecl) {}

bool FunctionLayoutOverride::VisitInstruction(llvm::Instruction &insn,
                                              clang::FunctionDecl *fdecl,
                                              clang::ValueDecl *&vdecl) {
  return false;
}

FunctionLayoutOverrideCombiner::FunctionLayoutOverrideCombiner(
    DecompilationContext &dec_ctx)
    : FunctionLayoutOverride(dec_ctx) {}

void FunctionLayoutOverrideCombiner::AddOverride(
    std::unique_ptr<FunctionLayoutOverride> provider) {
  overrides.push_back(std::move(provider));
}

bool FunctionLayoutOverrideCombiner::HasOverride(llvm::Function &func) {
  for (auto it{overrides.rbegin()}; it != overrides.rend(); ++it) {
    auto &override{*it};
    if (override->HasOverride(func)) {
      return true;
    }
  }
  return false;
}

std::vector<clang::QualType> FunctionLayoutOverrideCombiner::GetArguments(
    llvm::Function &func) {
  for (auto it{overrides.rbegin()}; it != overrides.rend(); ++it) {
    auto &override{*it};
    if (override->HasOverride(func)) {
      return override->GetArguments(func);
    }
  }
  return {};
}

void FunctionLayoutOverrideCombiner::BeginFunctionVisit(
    llvm::Function &func, clang::FunctionDecl *fdecl) {
  for (auto it{overrides.rbegin()}; it != overrides.rend(); ++it) {
    auto &override{*it};
    if (override->HasOverride(func)) {
      override->BeginFunctionVisit(func, fdecl);
      return;
    }
  }
}

bool FunctionLayoutOverrideCombiner::VisitInstruction(
    llvm::Instruction &insn, clang::FunctionDecl *fdecl,
    clang::ValueDecl *&vdecl) {
  for (auto it{overrides.rbegin()}; it != overrides.rend(); ++it) {
    auto &override{*it};
    if (override->HasOverride(*insn.getFunction())) {
      return override->VisitInstruction(insn, fdecl, vdecl);
    }
  }
  return false;
}

}  // namespace rellic