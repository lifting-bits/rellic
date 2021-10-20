/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include <llvm/IR/Metadata.h>
#define GOOGLE_STRIP_LOG 1

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <iterator>
#include <utility>

#include "rellic/AST/DebugInfoVisitor.h"

namespace rellic {

void DebugInfoVisitor::visitDbgDeclareInst(llvm::DbgDeclareInst& inst) {
  auto var{inst.getVariable()};
  auto loc{inst.getVariableLocation()};

  names[loc] = var->getName().str();
  scopes[loc] = var->getScope();
  valtypes[loc] = var->getType();
}

void DebugInfoVisitor::visitInstruction(llvm::Instruction& inst) {
  if (auto loc{inst.getDebugLoc().get()}) {
    scopes[&inst] = loc->getScope();
  }
}

void DebugInfoVisitor::WalkType(llvm::Type* type, llvm::DIType* ditype) {
  if (!ditype || types.find(type) != types.end()) {
    return;
  }

  while (auto derived{llvm::dyn_cast<llvm::DIDerivedType>(ditype)}) {
    // We are only interested in analyzing function types and structure types,
    // so we need to "unwrap" any DIDerivedType, of which there might be several
    // layers, in case of e.g. typedefs of typedefs, or pointers to pointers.
    ditype = derived->getBaseType();
    if (!ditype) {
      // This happens in the case of void pointers
      return;
    }
  }

  switch (type->getTypeID()) {
    case llvm::Type::FunctionTyID: {
      auto functype{llvm::cast<llvm::FunctionType>(type)};
      auto funcditype{llvm::cast<llvm::DISubroutineType>(ditype)};

      std::vector<llvm::Type*> type_array;
      type_array.push_back(functype->getReturnType());
      auto params{functype->params()};
      std::copy(params.begin(), params.end(), std::back_inserter(type_array));

      auto di_types{funcditype->getTypeArray()};
      if (type_array.size() != di_types.size()) {
        // Mismatch between bitcode and debug metadata, bail out
        break;
      }

      types[type] = ditype;
      for (auto i{0U}; i < type_array.size(); ++i) {
        WalkType(type_array[i], di_types[i]);
      }
    } break;
    case llvm::Type::StructTyID: {
      auto strcttype{llvm::cast<llvm::StructType>(type)};
      auto strctditype{llvm::cast<llvm::DICompositeType>(ditype)};

      auto elems{strcttype->elements()};
      auto di_elems{strctditype->getElements()};
      if (elems.size() != di_elems.size()) {
        // Mismatch between bitcode and debug metadata, bail out
        break;
      }

      types[type] = ditype;
      for (auto i{0U}; i < elems.size(); ++i) {
        auto field{llvm::cast<llvm::DIType>(di_elems[i])};
        WalkType(elems[i], field);
      }
    } break;
    case llvm::Type::PointerTyID: {
      auto ptrtype{llvm::cast<llvm::PointerType>(type)};
      WalkType(ptrtype->getElementType(), ditype);
    } break;
    case llvm::Type::ArrayTyID: {
      auto arrtype{llvm::cast<llvm::ArrayType>(type)};
      WalkType(arrtype->getElementType(), ditype);
    } break;
    default: {
      if (type->isVectorTy()) {
        auto vtype{llvm::cast<llvm::VectorType>(type)};
        WalkType(vtype->getElementType(), ditype);
      }
    } break;
  }
}

void DebugInfoVisitor::visitFunction(llvm::Function& func) {
  auto subprogram{func.getSubprogram()};
  if (!subprogram) {
    return;
  }

  auto ditype{subprogram->getType()};

  if (func.arg_size() + 1 != ditype->getTypeArray().size()) {
    // Debug metadata is not compatible with bitcode, bail out
    // TODO(frabert): Find a way to reconcile differences
    return;
  }

  funcs[&func] = ditype;
  size_t i{1};
  auto type_array{ditype->getTypeArray()};
  for (auto& arg : func.args()) {
    auto argtype{type_array[i++]};
    args[&arg] = argtype;
  }
  WalkType(func.getFunctionType(), ditype);
}

}  // namespace rellic
