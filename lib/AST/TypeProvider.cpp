/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/TypeProvider.h"

#include "rellic/AST/Util.h"

namespace rellic {
TypeProvider::TypeProvider(DecompilationContext& dec_ctx) : dec_ctx(dec_ctx) {}
TypeProvider::~TypeProvider() = default;

clang::QualType TypeProvider::GetFunctionReturnType(llvm::Function&) {
  return {};
}

clang::QualType TypeProvider::GetArgumentType(llvm::Argument&) { return {}; }

clang::QualType TypeProvider::GetGlobalVarType(llvm::GlobalVariable&) {
  return {};
}

// Defers to DecompilationContext::GetQualType
class FallbackTypeProvider : public TypeProvider {
 public:
  FallbackTypeProvider(DecompilationContext& dec_ctx);
  clang::QualType GetFunctionReturnType(llvm::Function& func) override;
  clang::QualType GetArgumentType(llvm::Argument& arg) override;
  clang::QualType GetGlobalVarType(llvm::GlobalVariable& gvar) override;
};

FallbackTypeProvider::FallbackTypeProvider(DecompilationContext& dec_ctx)
    : TypeProvider(dec_ctx) {}

clang::QualType FallbackTypeProvider::GetFunctionReturnType(
    llvm::Function& func) {
  return dec_ctx.GetQualType(func.getReturnType());
}

clang::QualType FallbackTypeProvider::GetArgumentType(llvm::Argument& arg) {
  return dec_ctx.GetQualType(arg.getType());
}

clang::QualType FallbackTypeProvider::GetGlobalVarType(
    llvm::GlobalVariable& gvar) {
  return dec_ctx.GetQualType(gvar.getValueType());
}

// Fixes function arguments that have a byval attribute
class ByValFixupTypeProvider : public TypeProvider {
 public:
  ByValFixupTypeProvider(DecompilationContext& dec_ctx);

  // This function fixes types for those arguments that are passed by value
  // using the `byval` attribute. They need special treatment because those
  // arguments, instead of actually being passed by value, are instead passed
  // "by reference" from a bitcode point of view, with the caveat that the
  // actual semantics are more like "create a copy of the reference before
  // calling, and pass a pointer to that copy instead" (this is done
  // implicitly). Thus, we need to convert a function type like
  //
  //   `i32 @do_foo(%struct.foo* byval(%struct.foo) align 4 %f)`
  //
  // into
  //
  //   `i32 @do_foo(%struct.foo %f)`
  clang::QualType GetArgumentType(llvm::Argument& arg) override;
};

ByValFixupTypeProvider::ByValFixupTypeProvider(DecompilationContext& dec_ctx)
    : TypeProvider(dec_ctx) {}

clang::QualType ByValFixupTypeProvider::GetArgumentType(llvm::Argument& arg) {
  if (!arg.hasByValAttr()) {
    return {};
  }

  auto byval{arg.getAttribute(llvm::Attribute::ByVal)};
  return dec_ctx.GetQualType(byval.getValueAsType());
}

// Fixes the function signature for `main`
class MainFuncTypeProvider : public TypeProvider {
 public:
  MainFuncTypeProvider(DecompilationContext& dec_ctx);
  clang::QualType GetFunctionReturnType(llvm::Function& func) override;
  clang::QualType GetArgumentType(llvm::Argument& arg) override;
};

MainFuncTypeProvider::MainFuncTypeProvider(DecompilationContext& dec_ctx)
    : TypeProvider(dec_ctx) {}
clang::QualType MainFuncTypeProvider::GetFunctionReturnType(
    llvm::Function& func) {
  if (func.getName() != "main") {
    return {};
  }
  return dec_ctx.ast_ctx.IntTy;
}

clang::QualType MainFuncTypeProvider::GetArgumentType(llvm::Argument& arg) {
  if (arg.getParent()->getName() != "main") {
    return {};
  }

  auto arg_no{arg.getArgNo()};
  switch (arg.getArgNo()) {
    case 0:  // argc
      return dec_ctx.ast_ctx.IntTy;
    case 1:  // argv and envp
    case 2: {
      auto char_ty{dec_ctx.ast_ctx.CharTy};
      auto char_ptr{dec_ctx.ast_ctx.getPointerType(char_ty)};
      auto char_ptr_ptr{dec_ctx.ast_ctx.getPointerType(char_ptr)};
      return char_ptr_ptr;
    }
    default:
      return {};
  }
}

TypeProviderCombiner::TypeProviderCombiner(DecompilationContext& dec_ctx)
    : TypeProvider(dec_ctx) {
  AddProvider<FallbackTypeProvider>();
  AddProvider<ByValFixupTypeProvider>();
  AddProvider<MainFuncTypeProvider>();
}

void TypeProviderCombiner::AddProvider(std::unique_ptr<TypeProvider> provider) {
  providers.push_back(std::move(provider));
}

clang::QualType TypeProviderCombiner::GetFunctionReturnType(
    llvm::Function& func) {
  for (auto it{providers.rbegin()}; it != providers.rend(); ++it) {
    auto& provider{*it};
    auto res{provider->GetFunctionReturnType(func)};
    if (!res.isNull()) {
      return res;
    }
  }
  return {};
}

clang::QualType TypeProviderCombiner::GetArgumentType(llvm::Argument& arg) {
  for (auto it{providers.rbegin()}; it != providers.rend(); ++it) {
    auto& provider{*it};
    auto res{provider->GetArgumentType(arg)};
    if (!res.isNull()) {
      return res;
    }
  }
  return {};
}

clang::QualType TypeProviderCombiner::GetGlobalVarType(
    llvm::GlobalVariable& gvar) {
  for (auto it{providers.rbegin()}; it != providers.rend(); ++it) {
    auto& provider{*it};
    auto res{provider->GetGlobalVarType(gvar)};
    if (!res.isNull()) {
      return res;
    }
  }
  return {};
}
}  // namespace rellic