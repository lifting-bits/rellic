/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Argument.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/Local.h>
#include <llvm/Transforms/Utils/LowerSwitch.h>

#include <unordered_map>
#include <unordered_set>

#include "rellic/BC/Util.h"

namespace rellic {
namespace {
static void MarkAsSigned(llvm::Instruction* inst) {
  auto& ctx{inst->getContext()};
  inst->setMetadata("rellic.signed", llvm::MDNode::get(ctx, {}));
}

static void MarkAsSigned(llvm::GlobalVariable* var) {
  auto& ctx{var->getContext()};
  var->setMetadata("rellic.signed", llvm::MDNode::get(ctx, {}));
}

static void DoTypeInference(llvm::Function& func) {
  std::unordered_set<llvm::Value*> signed_integers;
  size_t num_signed_values;

  do {
    num_signed_values = signed_integers.size();

    for (auto& inst : llvm::instructions(func)) {
      if (auto fptosi = llvm::dyn_cast<llvm::FPToSIInst>(&inst)) {
        signed_integers.insert(fptosi);
      }

      if (auto sitofp = llvm::dyn_cast<llvm::SIToFPInst>(&inst)) {
        signed_integers.insert(sitofp->getOperand(0));
      }

      if (auto icmp = llvm::dyn_cast<llvm::ICmpInst>(&inst)) {
        if (icmp->isSigned()) {
          signed_integers.insert(icmp->getOperand(0));
          signed_integers.insert(icmp->getOperand(1));
        }
      }

      if (auto sext = llvm::dyn_cast<llvm::SExtInst>(&inst)) {
        signed_integers.insert(sext);
        signed_integers.insert(sext->getOperand(0));
      }

      if (auto load = llvm::dyn_cast<llvm::LoadInst>(&inst)) {
        if (signed_integers.count(load)) {
          signed_integers.insert(load->getPointerOperand());
        }

        if (signed_integers.count(load->getPointerOperand())) {
          signed_integers.insert(load);
        }
      }

      if (auto store = llvm::dyn_cast<llvm::StoreInst>(&inst)) {
        if (signed_integers.count(store)) {
          signed_integers.insert(store->getPointerOperand());
        }

        if (signed_integers.count(store->getPointerOperand())) {
          signed_integers.insert(store);
        }
      }

      if (auto binop = llvm::dyn_cast<llvm::BinaryOperator>(&inst)) {
        switch (binop->getOpcode()) {
          case llvm::BinaryOperator::SRem:
          case llvm::BinaryOperator::SDiv:
            signed_integers.insert(binop);
            signed_integers.insert(binop->getOperand(0));
            signed_integers.insert(binop->getOperand(1));
            break;
          case llvm::BinaryOperator::Add:
          case llvm::BinaryOperator::Sub:
          case llvm::BinaryOperator::Mul:
            if (signed_integers.count(binop->getOperand(0)) ||
                signed_integers.count(binop->getOperand(1))) {
              signed_integers.insert(binop);
            }
          default:
            break;
        }
      }
    }
  } while (num_signed_values != signed_integers.size());

  for (auto value : signed_integers) {
    if (auto inst = llvm::dyn_cast<llvm::Instruction>(value)) {
      MarkAsSigned(inst);
    }

    if (auto var = llvm::dyn_cast<llvm::GlobalVariable>(value)) {
      MarkAsSigned(var);
    }
  }
}
}  // namespace

void PerformTypeInference(llvm::Module& module) {
  for (auto& func : module.functions()) {
    if (func.isDeclaration()) {
      continue;
    }

    DoTypeInference(func);
  }
}
}  // namespace rellic