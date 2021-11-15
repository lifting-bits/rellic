/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */
#define GOOGLE_STRIP_LOG 1

#include "rellic/AST/DebugInfoCollector.h"

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/Support/Casting.h>
#include <rellic/BC/Util.h>

#include <algorithm>
#include <iterator>
#include <utility>

namespace rellic {

void DebugInfoCollector::visitDbgDeclareInst(llvm::DbgDeclareInst& inst) {
  auto var{inst.getVariable()};
  auto loc{inst.getVariableLocation()};

  names[loc] = var->getName().str();
  scopes[loc] = var->getScope();
  valtypes[loc] = var->getType();

  WalkType(loc->getType(), var->getType());
}

void DebugInfoCollector::visitInstruction(llvm::Instruction& inst) {
  if (auto loc{inst.getDebugLoc().get()}) {
    scopes[&inst] = loc->getScope();
  }
}

void DebugInfoCollector::WalkType(llvm::Type* type, llvm::DIType* ditype) {
  if (!ditype || types.find(type) != types.end()) {
    return;
  }

  DLOG(INFO) << "Inspecting " << LLVMThingToString(ditype);
  if (auto funcditype = llvm::dyn_cast<llvm::DISubroutineType>(ditype)) {
    auto di_types{funcditype->getTypeArray()};

    llvm::FunctionType* functype{};
    if (type) {
      functype = llvm::dyn_cast<llvm::FunctionType>(type);
    }

    std::vector<llvm::Type*> type_array{};
    if (functype) {
      if (functype->getNumParams() + functype->isVarArg() + 1 !=
          di_types.size()) {
        DLOG(INFO) << "Associated function " << LLVMThingToString(type)
                   << " is not compatible";
        type_array.resize(di_types.size());
      } else {
        type_array.push_back(functype->getReturnType());
        auto params{functype->params()};
        std::copy(params.begin(), params.end(), std::back_inserter(type_array));
      }
      types[type] = ditype;
    } else {
      type_array.resize(di_types.size());
    }

    DLOG_IF(INFO, type && !functype)
        << "Associated type " << LLVMThingToString(type)
        << " is not a function";

    for (auto i{0U}; i < di_types.size(); ++i) {
      WalkType(type_array[i], di_types[i]);
    }
  } else if (auto composite = llvm::dyn_cast<llvm::DICompositeType>(ditype)) {
    std::vector<llvm::DIDerivedType*> di_fields{};
    for (auto elem : composite->getElements()) {
      if (auto field = llvm::dyn_cast<llvm::DIDerivedType>(elem)) {
        auto tag{field->getTag()};
        if (tag == llvm::dwarf::DW_TAG_member ||
            tag == llvm::dwarf::DW_TAG_inheritance) {
          di_fields.push_back(field);
        }
      }
    }

    if (composite->getTag() == llvm::dwarf::DW_TAG_array_type) {
      llvm::Type* basetype{};

      if (type) {
        if (composite->getFlags() &
            llvm::DICompositeType::DIFlags::FlagVector) {
          if (auto arrtype = llvm::dyn_cast<llvm::ArrayType>(type)) {
            basetype = arrtype->getElementType();
          } else {
            DLOG(INFO) << "Associated type " << LLVMThingToString(type)
                       << " is not an array";
          }
        } else {
          if (auto vectype = llvm::dyn_cast<llvm::VectorType>(type)) {
            basetype = vectype->getElementType();
          } else {
            DLOG(INFO) << "Associated type " << LLVMThingToString(type)
                       << " is not a vector";
          }
        }
        types[type] = ditype;
      }

      WalkType(basetype, composite->getBaseType());
    } else {
      llvm::StructType* strcttype{};
      if (type) {
        strcttype = llvm::dyn_cast<llvm::StructType>(type);
      }

      std::vector<llvm::Type*> elems{};
      if (strcttype) {
        if (strcttype->getNumElements() != di_fields.size()) {
          elems.resize(di_fields.size());
          DLOG(INFO) << "Associated struct " << LLVMThingToString(type)
                     << " is not compatible";
        } else {
          elems = strcttype->elements();
        }
        types[type] = ditype;
      } else {
        elems.resize(di_fields.size());
      }

      DLOG_IF(INFO, type && !strcttype)
          << "Associated type " << LLVMThingToString(type)
          << " is not a struct";

      structs.push_back(composite);
      for (auto i{0U}; i < di_fields.size(); ++i) {
        WalkType(elems[i], di_fields[i]);
      }
    }
  } else if (auto derived = llvm::dyn_cast<llvm::DIDerivedType>(ditype)) {
    auto baseditype{derived->getBaseType()};
    switch (derived->getTag()) {
      case llvm::dwarf::DW_TAG_pointer_type: {
        llvm::PointerType* ptrtype{};
        if (type) {
          ptrtype = llvm::dyn_cast<llvm::PointerType>(type);
        }

        DLOG_IF(INFO, type && !ptrtype)
            << "Associated type " << LLVMThingToString(type)
            << " is not a pointer";

        auto basetype{ptrtype ? ptrtype->getElementType() : nullptr};
        types[type] = ditype;
        WalkType(basetype, baseditype);
      } break;
      default:
        // We are only interested in analyzing function types and structure
        // types, so we need to "unwrap" any DIDerivedType, of which there might
        // be several layers, in case of e.g. typedefs of typedefs, or pointers
        // to pointers.
        //
        // Practical example: given the following source code
        //
        //   struct foo {
        //     int field;
        //   };
        //   typedef struct foo foo_t;
        //   int main(void) {
        //     const volatile foo_t **a;
        //   }
        //
        // The variable `a` will be annotated with
        // DIDerivedType             (pointer)
        // - DIDerivedType           (pointer)
        //   - DIDerivedType         (const)
        //     - DIDerivedType       (volatile)
        //       - DIDerivedType     (typedef)
        //         - DICompositeType (struct)
        //
        // We are only interested in walking the actual struct. Note that the
        // information about e.g. const volatile is not lost, as the original
        // DIDerivedType is still associated with the original llvm::Value by
        // visitDbgDeclareInst
        WalkType(type, baseditype);
        break;
    }
  }
}

void DebugInfoCollector::visitFunction(llvm::Function& func) {
  auto subprogram{func.getSubprogram()};
  if (!subprogram) {
    return;
  }

  auto ditype{subprogram->getType()};
  auto type_array{ditype->getTypeArray()};
  if (func.arg_size() + func.isVarArg() + 1 == type_array.size()) {
    funcs[&func] = ditype;
    size_t i{1};
    for (auto& arg : func.args()) {
      auto argtype{type_array[i++]};
      args[&arg] = argtype;
    }
  } else {
    // Debug metadata is not compatible with bitcode, bail out
    // TODO(frabert): Find a way to reconcile differences
  }
  WalkType(func.getFunctionType(), ditype);
}

}  // namespace rellic
