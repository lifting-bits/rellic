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

#include "rellic/AST/DecompilationContext.h"
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

bool FunctionLayoutOverride::NeedsDereference(llvm::Function &func,
                                              llvm::Value &val) {
  return false;
}

class FallbackFunctionLayoutOverride : public FunctionLayoutOverride {
 public:
  FallbackFunctionLayoutOverride(DecompilationContext &dec_ctx)
      : FunctionLayoutOverride(dec_ctx) {}

  bool HasOverride(llvm::Function &func) final { return true; }

  std::vector<clang::QualType> GetArguments(llvm::Function &func) final {
    std::vector<clang::QualType> arg_types;
    for (auto &arg : func.args()) {
      arg_types.push_back(dec_ctx.type_provider->GetArgumentType(arg));
    }
    return arg_types;
  }

  void BeginFunctionVisit(llvm::Function &func,
                          clang::FunctionDecl *fdecl) final {
    std::vector<clang::ParmVarDecl *> params;
    for (auto &arg : func.args()) {
      auto &parm{dec_ctx.value_decls[&arg]};
      if (parm) {
        return;
      }
      // Create a name
      auto name{arg.hasName() ? arg.getName().str()
                              : "arg" + std::to_string(arg.getArgNo())};
      // Get parent function declaration
      auto func{arg.getParent()};
      auto fdecl{clang::cast<clang::FunctionDecl>(dec_ctx.value_decls[func])};
      auto argtype = dec_ctx.type_provider->GetArgumentType(arg);
      // Create a declaration
      parm = dec_ctx.ast.CreateParamDecl(fdecl, argtype, name);
      params.push_back(
          clang::dyn_cast<clang::ParmVarDecl>(dec_ctx.value_decls[&arg]));
    }

    fdecl->setParams(params);
  }

  bool VisitInstruction(llvm::Instruction &inst, clang::FunctionDecl *fdecl,
                        clang::ValueDecl *&vdecl) final {
    return false;
  }

  bool NeedsDereference(llvm::Function &func, llvm::Value &val) final {
    return llvm::isa<llvm::AllocaInst>(val);
  }
};

FunctionLayoutOverrideCombiner::FunctionLayoutOverrideCombiner(
    DecompilationContext &dec_ctx)
    : FunctionLayoutOverride(dec_ctx) {
  AddOverride<FallbackFunctionLayoutOverride>();
}

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

bool FunctionLayoutOverrideCombiner::NeedsDereference(llvm::Function &func,
                                                      llvm::Value &val) {
  for (auto it{overrides.rbegin()}; it != overrides.rend(); ++it) {
    auto &override{*it};
    if (override->HasOverride(func)) {
      return override->NeedsDereference(func, val);
    }
  }
  return false;
}

}  // namespace rellic