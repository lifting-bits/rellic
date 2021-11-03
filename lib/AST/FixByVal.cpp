/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#define GOOGLE_STRIP_LOG 1

#include "rellic/AST/FixByVal.h"

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Transforms/Utils/Cloning.h>

namespace rellic {
char FixByVal::ID = 0;

FixByVal::FixByVal() : llvm::ModulePass(ID) {}

void FixByVal::visitFunction(llvm::Function& old_func) {
  auto module{old_func.getParent()};
  std::vector<llvm::Type*> new_arg_types{};
  auto has_byval{false};

  for (auto& arg : old_func.args()) {
    if (arg.hasByValAttr()) {
      auto ptrtype{llvm::cast<llvm::PointerType>(arg.getType())};
      new_arg_types.push_back(ptrtype->getElementType());
      has_byval = true;
    } else {
      new_arg_types.push_back(arg.getType());
    }
  }

  if (!has_byval) {
    return;
  }

  auto addrspace{old_func.getAddressSpace()};
  auto new_func_type{llvm::FunctionType::get(
      old_func.getReturnType(), new_arg_types, old_func.isVarArg())};
  auto new_func{llvm::Function::Create(new_func_type, old_func.getLinkage(),
                                       addrspace, old_func.getName(), module)};
  new_func->setSubprogram(old_func.getSubprogram());
  auto bb{llvm::BasicBlock::Create(module->getContext(), "", new_func)};
  llvm::IRBuilder<> builder{bb};
  llvm::ValueToValueMapTy map{};

  for (auto i{0U}; i < old_func.arg_size(); ++i) {
    auto old_arg{old_func.getArg(i)};
    auto new_arg{new_func->getArg(i)};
    if (old_arg->hasByValAttr()) {
      new_arg->setName(old_arg->getName());
      auto ptrtype{llvm::cast<llvm::PointerType>(old_arg->getType())};
      auto alloca_arg{builder.CreateAlloca(ptrtype->getElementType())};
      builder.CreateStore(new_arg, alloca_arg);

      map[old_arg] = alloca_arg;
    } else {
      new_arg->setName(old_arg->getName());
      map[old_arg] = new_arg;
    }
  }
  llvm::SmallVector<llvm::ReturnInst*> rets{};
  llvm::CloneFunctionInto(new_func, &old_func, map, true, rets);
  builder.CreateBr(new_func->getBasicBlockList().getNextNode(*bb));
  subst[&old_func] = new_func;
  old_func.removeFromParent();
}

void FixByVal::visitCallInst(llvm::CallInst& call) {
  auto subst_func{subst[call.getCalledFunction()]};
  if (subst_func) {
    call.setCalledFunction(subst_func);
    for (auto i{0U}; i < call.getNumArgOperands(); ++i) {
      call.removeParamAttr(i, llvm::Attribute::ByVal);
    }
  }
}

bool FixByVal::runOnModule(llvm::Module& M) {
  visit(M);
  return true;
}
}  // namespace rellic