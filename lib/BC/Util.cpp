/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/BC/Util.h"

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Transforms/Utils/Local.h>
#include <llvm/Transforms/Utils/LowerSwitch.h>

#include <unordered_map>

namespace rellic {

namespace {

// Convert an LLVM thing (e.g. `llvm::Value` or `llvm::Type`) into
// a `std::string`.
template <typename T>
inline static std::string DoLLVMThingToString(T *thing) {
  if (thing) {
    std::string str;
    llvm::raw_string_ostream str_stream(str);
    thing->print(str_stream);
    return str;
  } else {
    return "(null)";
  }
}

}  // namespace

std::string LLVMThingToString(llvm::Value *thing) {
  return DoLLVMThingToString(thing);
}

std::string LLVMThingToString(llvm::Type *thing) {
  return DoLLVMThingToString(thing);
}

std::string LLVMThingToString(llvm::DIType *thing) {
  return DoLLVMThingToString(thing);
}

// Try to verify a module.
bool VerifyModule(llvm::Module *module) {
  std::string error;
  llvm::raw_string_ostream error_stream(error);
  if (llvm::verifyModule(*module, &error_stream)) {
    error_stream.flush();
    LOG(ERROR) << "Error verifying module read from file: " << error;
    return false;
  } else {
    return true;
  }
}

// Reads an LLVM module from a file.
llvm::Module *LoadModuleFromFile(llvm::LLVMContext *context,
                                 std::string file_name, bool allow_failure) {
  llvm::SMDiagnostic err;
  auto mod_ptr = llvm::parseIRFile(file_name, err, *context);
  auto module = mod_ptr.release();

  if (!module) {
    LOG_IF(FATAL, !allow_failure) << "Unable to parse module file " << file_name
                                  << ": " << err.getMessage().str();
    return nullptr;
  }

  auto ec = module->materializeAll();  // Just in case.
  if (ec) {
    LOG_IF(FATAL, !allow_failure)
        << "Unable to materialize everything from " << file_name;
    delete module;
    return nullptr;
  }

  if (!VerifyModule(module)) {
    LOG_IF(FATAL, !allow_failure)
        << "Error verifying module read from file " << file_name;
    delete module;
    return nullptr;
  }

  return module;
}

llvm::Module *LoadModuleFromMemory(llvm::LLVMContext *context,
                                   std::string file_data, bool allow_failure) {
  llvm::SMDiagnostic err;
  llvm::MemoryBufferRef ref(file_data, "memory");
  auto mod_ptr = llvm::parseIR(ref, err, *context);
  auto module = mod_ptr.release();

  if (!module) {
    LOG_IF(FATAL, !allow_failure)
        << "Unable to parse module file: " << err.getMessage().str();
    return nullptr;
  }

  auto ec = module->materializeAll();  // Just in case.
  if (ec) {
    LOG_IF(FATAL, !allow_failure) << "Unable to materialize everything";
    delete module;
    return nullptr;
  }

  if (!VerifyModule(module)) {
    LOG_IF(FATAL, !allow_failure) << "Error verifying module";
    delete module;
    return nullptr;
  }

  return module;
}

bool IsGlobalMetadata(const llvm::GlobalObject &go) {
  return go.getSection() == "llvm.metadata";
}

bool IsAnnotationIntrinsic(llvm::Intrinsic::ID id) {
  // this is a copy of IntrinsicInst::isAssumeLikeIntrinsic in LLVM12+
  // NOTE(artem): This probalby needs some compat wrappers for older LLVM
  switch (id) {
    case llvm::Intrinsic::assume:
    case llvm::Intrinsic::sideeffect:
    // case llvm::Intrinsic::pseudoprobe:
    case llvm::Intrinsic::dbg_declare:
    case llvm::Intrinsic::dbg_value:
    case llvm::Intrinsic::dbg_label:
    case llvm::Intrinsic::invariant_start:
    case llvm::Intrinsic::invariant_end:
    case llvm::Intrinsic::lifetime_start:
    case llvm::Intrinsic::lifetime_end:
    // case llvm::Intrinsic::experimental_noalias_scope_decl:
    case llvm::Intrinsic::objectsize:
    case llvm::Intrinsic::ptr_annotation:
    case llvm::Intrinsic::var_annotation:
      return true;
    default:
      return false;
  }
}

void CloneMetadataInto(
    llvm::Instruction *dst,
    const llvm::SmallVector<std::pair<unsigned, llvm::MDNode *>, 16u> &mds) {
  for (auto [id, node] : mds) {
    switch (id) {
      case llvm::LLVMContext::MD_tbaa:
      case llvm::LLVMContext::MD_tbaa_struct:
      case llvm::LLVMContext::MD_noalias:
      case llvm::LLVMContext::MD_alias_scope:
        break;
      default:
        dst->setMetadata(id, node);
        break;
    }
  }
}

void CopyMetadataTo(llvm::Value *src, llvm::Value *dst) {
  if (src == dst) {
    return;
  }
  llvm::Instruction *src_inst = llvm::dyn_cast_or_null<llvm::Instruction>(src),
                    *dst_inst = llvm::dyn_cast_or_null<llvm::Instruction>(dst);
  if (!src_inst || !dst_inst) {
    return;
  }

  llvm::SmallVector<std::pair<unsigned, llvm::MDNode *>, 16u> mds;
  src_inst->getAllMetadataOtherThanDebugLoc(mds);
  CloneMetadataInto(dst_inst, mds);
}

void RemovePHINodes(llvm::Module &module) {
  std::vector<llvm::PHINode *> work_list;
  for (auto &func : module) {
    for (auto &inst : llvm::instructions(func)) {
      if (auto phi = llvm::dyn_cast<llvm::PHINode>(&inst)) {
        work_list.push_back(phi);
      }
    }
  }
  for (auto phi : work_list) {
    llvm::SmallVector<std::pair<unsigned, llvm::MDNode *>, 16u> mds;
    phi->getAllMetadataOtherThanDebugLoc(mds);
    auto new_alloca{DemotePHIToStack(phi)};
    CloneMetadataInto(new_alloca, mds);
  }
}

void LowerSwitches(llvm::Module &module) {
  llvm::PassBuilder pb;
  llvm::ModulePassManager mpm;
  llvm::ModuleAnalysisManager mam;
  llvm::LoopAnalysisManager lam;
  llvm::CGSCCAnalysisManager cam;
  llvm::FunctionAnalysisManager fam;

  pb.registerFunctionAnalyses(fam);
  pb.registerModuleAnalyses(mam);
  pb.registerCGSCCAnalyses(cam);
  pb.registerLoopAnalyses(lam);

  pb.crossRegisterProxies(lam, fam, cam, mam);

  llvm::FunctionPassManager fpm;
  fpm.addPass(llvm::LowerSwitchPass());

  mpm.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(fpm)));
  mpm.run(module, mam);

  mam.clear();
  fam.clear();
  cam.clear();
  lam.clear();
  CHECK(VerifyModule(&module)) << "Transformation broke module correctness";
}

// Takes an insertvalue node and converts into an alloca, store, load sequence
// and returns the load
static llvm::LoadInst *ConvertInsertValue(llvm::InsertValueInst *I) {
  if (I->use_empty()) {
    I->removeFromParent();
    return nullptr;
  }

  auto m{I->getModule()};
  auto &ctx{m->getContext()};
  auto DL{m->getDataLayout()};

  auto F{I->getParent()->getParent()};
  auto alloca{new llvm::AllocaInst(I->getType(), DL.getAllocaAddrSpace(),
                                   nullptr, I->getName() + ".iv2mem", I)};
  auto aggr_opnd{I->getAggregateOperand()};
  auto aggr_ty{aggr_opnd->getType()};
  auto ins_opnd{I->getInsertedValueOperand()};

  if (!llvm::isa<llvm::UndefValue>(aggr_opnd)) {
    new llvm::StoreInst(aggr_opnd, alloca, I);
  }
  std::vector<llvm::Value *> indices;
  indices.push_back(llvm::ConstantInt::get(ctx, llvm::APInt(64, 0, false)));
  for (auto i : I->getIndices()) {
    indices.push_back(
        llvm::ConstantInt::get(ctx, llvm::APInt(sizeof(i) * 8, i)));
  }
  auto ptr{llvm::GetElementPtrInst::Create(aggr_opnd->getType(), alloca,
                                           indices, "", I)};
  new llvm::StoreInst(ins_opnd, ptr, I);
  auto load{
      new llvm::LoadInst(I->getType(), alloca, I->getName() + ".reload", I)};

  I->replaceAllUsesWith(load);
  I->eraseFromParent();
  return load;
}

void RemoveInsertValues(llvm::Module &m) {
  std::vector<llvm::InsertValueInst *> work_list;
  for (auto &func : m) {
    for (auto &inst : llvm::instructions(func)) {
      if (auto iv = llvm::dyn_cast<llvm::InsertValueInst>(&inst)) {
        work_list.push_back(iv);
      }
    }
  }

  for (auto iv : work_list) {
    llvm::SmallVector<std::pair<unsigned, llvm::MDNode *>, 16u> mds;
    iv->getAllMetadataOtherThanDebugLoc(mds);
    auto new_load{ConvertInsertValue(iv)};
    CloneMetadataInto(new_load, mds);
  }

  CHECK(VerifyModule(&m)) << "Transformation broke module correctness";
}

void ConvertArrayArguments(llvm::Module &m) {
  std::unordered_map<llvm::Type *, llvm::Type *> conv_types;
  std::vector<unsigned> indices;
  indices.push_back(0);
  std::vector<llvm::Function *> funcs_to_remove;
  auto &ctx{m.getContext()};
  auto ConvertType = [&](llvm::Type *t) -> llvm::Type * {
    if (!t->isArrayTy()) {
      return t;
    }

    auto &ty{conv_types[t]};
    if (!ty) {
      llvm::Type *types[] = {t};
      ty = llvm::StructType::create({types, 1}, "aggr2struct");
    }

    return ty;
  };

  auto ConvertFunction = [&](llvm::Function *orig_func) -> llvm::Function * {
    auto return_ty{ConvertType(orig_func->getReturnType())};
    std::vector<llvm::Type *> arg_types;
    for (auto &arg : orig_func->args()) {
      arg_types.push_back(ConvertType(arg.getType()));
    }
    auto func_type{
        llvm::FunctionType::get(return_ty, arg_types, orig_func->isVarArg())};
    auto new_func{llvm::Function::Create(func_type, orig_func->getLinkage(),
                                         orig_func->getName(), m)};
    if (orig_func->isDeclaration()) {
      return new_func;
    }
    auto bb{llvm::BasicBlock::Create(m.getContext(), "", new_func)};

    llvm::ValueToValueMapTy ValueMap;
    auto new_args{new_func->arg_begin()};
    for (auto &old_arg : orig_func->args()) {
      new_args->setName(old_arg.getName());
      if (old_arg.getType()->isArrayTy()) {
        ValueMap[&old_arg] =
            llvm::ExtractValueInst::Create(new_args, indices, "", bb);
      } else {
        ValueMap[&old_arg] = new_args;
      }
      ++new_args;
    }
    llvm::SmallVector<llvm::ReturnInst *, 8> Returns;
    llvm::CloneFunctionInto(new_func, orig_func, ValueMap,
                            llvm::CloneFunctionChangeType::LocalChangesOnly,
                            Returns);
    llvm::BranchInst::Create(bb->getNextNode(), bb);

    if (orig_func->getReturnType()->isArrayTy()) {
      auto undef{llvm::UndefValue::get(return_ty)};
      for (auto ret : Returns) {
        auto wrap{llvm::InsertValueInst::Create(undef, ret->getReturnValue(),
                                                indices, "", ret)};
        auto new_ret{llvm::ReturnInst::Create(ctx, wrap, ret)};
        ret->eraseFromParent();
      }
    }
    funcs_to_remove.push_back(orig_func);
    return new_func;
  };

  std::unordered_map<llvm::Function *, llvm::Function *> fmap;
  for (auto &f : m.functions()) {
    if (f.getReturnType()->isArrayTy()) {
      fmap[&f] = ConvertFunction(&f);
    } else {
      for (auto &arg : f.args()) {
        if (arg.getType()->isArrayTy()) {
          fmap[&f] = ConvertFunction(&f);
          break;
        }
      }
    }
  }

  for (auto &f : m.functions()) {
    std::vector<llvm::Instruction *> insts_to_remove;
    for (auto &i : llvm::instructions(f)) {
      if (auto call = llvm::dyn_cast<llvm::CallInst>(&i)) {
        auto callee{call->getCalledFunction()};
        auto new_func{fmap[callee]};
        if (!new_func) {
          continue;
        }

        std::vector<llvm::Value *> args;
        for (auto &old_arg : call->args()) {
          if (old_arg->getType()->isArrayTy()) {
            auto undef{llvm::UndefValue::get(conv_types[old_arg->getType()])};
            auto new_arg{llvm::InsertValueInst::Create(undef, old_arg, indices,
                                                       "", call)};
            args.push_back(new_arg);
          } else {
            args.push_back(old_arg);
          }
        }
        llvm::SmallVector<std::pair<unsigned, llvm::MDNode *>, 16u> mds;
        auto new_call{llvm::CallInst::Create(new_func->getFunctionType(),
                                             new_func, args, call->getName(),
                                             call)};
        call->getAllMetadata(mds);
        CloneMetadataInto(new_call, mds);
        if (callee->getReturnType()->isArrayTy()) {
          auto unwrap{
              llvm::ExtractValueInst::Create(new_call, indices, "", call)};
          call->replaceAllUsesWith(unwrap);
        } else {
          call->replaceAllUsesWith(new_call);
        }
        insts_to_remove.push_back(call);
      }
    }

    for (auto inst : insts_to_remove) {
      inst->eraseFromParent();
    }
  }

  for (auto func : funcs_to_remove) {
    // TODO(frabert): Sometimes uses stick around which are not calls (e.g.
    // references in globals). How do we replace those? Cannot use
    // `func->replaceAllUsesWith` because types don't match.
    if (func->use_empty()) {
      func->eraseFromParent();
    }
  }

  CHECK(VerifyModule(&m)) << "Transformation broke module correctness";
}

}  // namespace rellic