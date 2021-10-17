/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#define GOOGLE_STRIP_LOG 1

#include "rellic/AST/DebugInfoVisitor.h"

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/Support/Casting.h>

#include <algorithm>
#include <iterator>
#include <utility>

#include "rellic/AST/Util.h"
#include "rellic/BC/Util.h"

namespace rellic {

void DebugInfoVisitor::visitDbgDeclareInst(llvm::DbgDeclareInst& inst) {
  auto* var = inst.getVariable();
  auto* loc = inst.getVariableLocation();

  names[loc] = var->getName().str();
  scopes[loc] = var->getScope();
  valtypes[loc] = var->getType();
}

void DebugInfoVisitor::visitInstruction(llvm::Instruction& inst) {
  if (auto* loc = inst.getDebugLoc().get()) {
    scopes[&inst] = loc->getScope();
  }
}

void DebugInfoVisitor::walkType(llvm::Type* type, llvm::DIType* ditype) {
  if (!ditype || types.find(type) != types.end()) {
    return;
  }

  while (auto* derived = llvm::dyn_cast<llvm::DIDerivedType>(ditype)) {
    ditype = derived->getBaseType();
    if (!ditype) {
      return;
    }
  }

  switch (type->getTypeID()) {
    case llvm::Type::FunctionTyID: {
      auto* functype = llvm::cast<llvm::FunctionType>(type);
      auto* funcditype = llvm::cast<llvm::DISubroutineType>(ditype);
      types[type] = ditype;

      std::vector<llvm::Type*> type_array;
      type_array.push_back(functype->getReturnType());
      auto params = functype->params();
      std::copy(params.begin(), params.end(), std::back_inserter(type_array));

      auto di_types = funcditype->getTypeArray();
      if (type_array.size() != di_types.size()) {
        // Mismatch between bitcode and debug metadata, bail out
        break;
      }
      for (size_t i = 0; i < type_array.size(); i++) {
        walkType(type_array[i], di_types[i]);
      }
    } break;
    case llvm::Type::StructTyID: {
      auto* strcttype = llvm::cast<llvm::StructType>(type);
      auto* strctditype = llvm::cast<llvm::DICompositeType>(ditype);
      types[type] = ditype;

      auto elems = strcttype->elements();
      auto di_elems = strctditype->getElements();
      if (elems.size() != di_elems.size()) {
        // Mismatch between bitcode and debug metadata, bail out
        break;
      }
      for (size_t i = 0; i < elems.size(); i++) {
        auto* field = llvm::cast<llvm::DIType>(di_elems[i]);
        walkType(elems[i], field);
      }
    } break;
    case llvm::Type::PointerTyID: {
      auto* ptrtype = llvm::cast<llvm::PointerType>(type);
      walkType(ptrtype->getElementType(), ditype);
    } break;
    case llvm::Type::ArrayTyID: {
      auto* arrtype = llvm::cast<llvm::ArrayType>(type);
      walkType(arrtype->getElementType(), ditype);
    } break;
    default: {
      if (type->isVectorTy()) {
        auto* vtype = llvm::cast<llvm::VectorType>(type);
        walkType(vtype->getElementType(), ditype);
      }
    } break;
  }
}

void DebugInfoVisitor::visitFunction(llvm::Function& func) {
  if (auto* subprogram = func.getSubprogram()) {
    auto* ditype = subprogram->getType();
    funcs[&func] = ditype;

    if (func.arg_size() + 1 != ditype->getTypeArray().size()) {
      // Debug metadata is not compatible with bitcode, bail out
      // FIXME: Find a way to reconcile differences
      return;
    }

    CHECK(func.arg_size() + 1 == ditype->getTypeArray().size());
    size_t i = 1;
    for (auto& arg : func.args()) {
      auto* argtype = ditype->getTypeArray()[i++];
      args[&arg] = argtype;
    }
    walkType(func.getFunctionType(), ditype);
  }
}

}  // namespace rellic
