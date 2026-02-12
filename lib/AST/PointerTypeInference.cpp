/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/PointerTypeInference.h"

#include <glog/logging.h>
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>

#include "rellic/AST/DebugInfoCollector.h"
#include "rellic/BC/Util.h"

// PointerTypeInferenceAnalysis implementation

llvm::Type* rellic::PointerTypeInferenceAnalysis::GetPointeeType(
    llvm::Value* ptr_value) const {
  auto it = value_to_pointee_type_.find(ptr_value);
  if (it != value_to_pointee_type_.end()) {
    return it->second;
  }
  return nullptr;
}

bool rellic::PointerTypeInferenceAnalysis::RecordInference(llvm::Value* ptr_value,
                                                           llvm::Type* pointee_type,
                                                           TypeSource source) {
  if (!ptr_value || !pointee_type) {
    return false;
  }

  // Don't infer void types (that's our fallback anyway)
  if (pointee_type->isVoidTy()) {
    return false;
  }

  auto existing_it = value_to_pointee_type_.find(ptr_value);

  if (existing_it != value_to_pointee_type_.end()) {
    // Already have an inference for this value
    auto existing_source = inference_source_[ptr_value];

    if (static_cast<int>(source) > static_cast<int>(existing_source)) {
      // Higher priority source, override
      DLOG(INFO) << "Overriding inference for " << LLVMThingToString(ptr_value)
                 << " with higher priority type: "
                 << LLVMThingToString(pointee_type);
      value_to_pointee_type_[ptr_value] = pointee_type;
      inference_source_[ptr_value] = source;
      stats_.conflicts_resolved++;
      return true;
    } else if (static_cast<int>(source) == static_cast<int>(existing_source)) {
      // Same priority
      if (existing_it->second != pointee_type) {
        // Different types at same priority - log warning but keep existing
        DLOG(WARNING) << "Type conflict for " << LLVMThingToString(ptr_value)
                      << ": existing=" << LLVMThingToString(existing_it->second)
                      << ", new=" << LLVMThingToString(pointee_type)
                      << " (keeping existing)";
        stats_.conflicts_resolved++;
      }
      return false;
    } else {
      // Lower priority, ignore
      return false;
    }
  } else {
    // New inference
    value_to_pointee_type_[ptr_value] = pointee_type;
    inference_source_[ptr_value] = source;
    stats_.total_inferences++;

    switch (source) {
      case TypeSource::CUSTOM_METADATA:
        stats_.custom_metadata++;
        break;
      case TypeSource::USAGE_ANALYSIS:
        stats_.usage_based++;
        break;
      case TypeSource::DEBUG_INFO:
        stats_.debug_info++;
        break;
      case TypeSource::PROPAGATION:
        stats_.propagation++;
        break;
      default:
        break;
    }

    DLOG(INFO) << "Inferred type for " << LLVMThingToString(ptr_value) << ": "
               << LLVMThingToString(pointee_type) << " from "
               << static_cast<int>(source);
    return true;
  }
}

rellic::PointerTypeInferenceAnalysis::TypeSource
rellic::PointerTypeInferenceAnalysis::GetInferenceSource(llvm::Value* ptr_value) const {
  auto it = inference_source_.find(ptr_value);
  if (it != inference_source_.end()) {
    return it->second;
  }
  return TypeSource::UNKNOWN;
}

rellic::PointerTypeInferenceAnalysis::Statistics
rellic::PointerTypeInferenceAnalysis::GetStatistics() const {
  return stats_;
}

// Helper: Extract pointee type from DIType (debug info)
static llvm::Type* ExtractPointeeTypeFromDIType(
    llvm::DIType* ditype, llvm::Module& module,
    std::unordered_map<llvm::DIType*, llvm::Type*>& cache,
    std::unordered_set<llvm::DIType*>& visiting);

// Convert DIBasicType to LLVM Type
static llvm::Type* DIBasicTypeToLLVMType(llvm::DIBasicType* basic_type,
                                         llvm::LLVMContext& ctx) {
  if (!basic_type) {
    return nullptr;
  }

  auto encoding = basic_type->getEncoding();
  auto size_in_bits = basic_type->getSizeInBits();

  switch (encoding) {
    case llvm::dwarf::DW_ATE_signed:
    case llvm::dwarf::DW_ATE_unsigned:
    case llvm::dwarf::DW_ATE_boolean:
    case llvm::dwarf::DW_ATE_signed_char:
    case llvm::dwarf::DW_ATE_unsigned_char:
      // Integer types
      if (size_in_bits == 0) {
        return nullptr;
      }
      return llvm::Type::getIntNTy(ctx, size_in_bits);

    case llvm::dwarf::DW_ATE_float:
      // Floating point types
      if (size_in_bits == 32) {
        return llvm::Type::getFloatTy(ctx);
      } else if (size_in_bits == 64) {
        return llvm::Type::getDoubleTy(ctx);
      } else if (size_in_bits == 16) {
        return llvm::Type::getHalfTy(ctx);
      } else if (size_in_bits == 128) {
        return llvm::Type::getFP128Ty(ctx);
      }
      return nullptr;

    default:
      return nullptr;
  }
}

// Convert DICompositeType to LLVM Type
static llvm::Type* DICompositeTypeToLLVMType(llvm::DICompositeType* comp_type,
                                             llvm::Module& module) {
  if (!comp_type) {
    return nullptr;
  }

  auto name = comp_type->getName();
  if (name.empty()) {
    return nullptr;
  }

  // Try to find a struct type with this name in the module
  llvm::StructType* found_struct = nullptr;

  // Try exact name match first
  found_struct = llvm::StructType::getTypeByName(module.getContext(), name);
  if (found_struct) {
    return found_struct;
  }

  // Try with "struct." prefix
  std::string struct_name = "struct." + name.str();
  found_struct =
      llvm::StructType::getTypeByName(module.getContext(), struct_name);
  if (found_struct) {
    return found_struct;
  }

  // Try with "union." prefix for unions
  std::string union_name = "union." + name.str();
  found_struct = llvm::StructType::getTypeByName(module.getContext(), union_name);
  if (found_struct) {
    return found_struct;
  }

  // Try with "class." prefix for C++ classes
  std::string class_name = "class." + name.str();
  found_struct = llvm::StructType::getTypeByName(module.getContext(), class_name);
  if (found_struct) {
    return found_struct;
  }

  return nullptr;
}

// Recursive helper for ExtractPointeeTypeFromDIType
static llvm::Type* ExtractPointeeTypeFromDIType(
    llvm::DIType* ditype, llvm::Module& module,
    std::unordered_map<llvm::DIType*, llvm::Type*>& cache,
    std::unordered_set<llvm::DIType*>& visiting) {
  if (!ditype) {
    return nullptr;
  }

  // Check cache first
  auto cache_it = cache.find(ditype);
  if (cache_it != cache.end()) {
    return cache_it->second;
  }

  // Check for cycles
  if (visiting.count(ditype)) {
    // Cycle detected - create opaque struct
    auto opaque_struct = llvm::StructType::create(module.getContext(), "struct.recursive");
    cache[ditype] = opaque_struct;
    return opaque_struct;
  }

  visiting.insert(ditype);

  llvm::Type* result = nullptr;

  if (auto basic_type = llvm::dyn_cast<llvm::DIBasicType>(ditype)) {
    result = DIBasicTypeToLLVMType(basic_type, module.getContext());
  } else if (auto comp_type = llvm::dyn_cast<llvm::DICompositeType>(ditype)) {
    result = DICompositeTypeToLLVMType(comp_type, module);
  } else if (auto derived_type = llvm::dyn_cast<llvm::DIDerivedType>(ditype)) {
    // Unwrap derived types (typedefs, const, volatile, pointer, etc.)
    auto base_type = derived_type->getBaseType();
    auto tag = derived_type->getTag();

    switch (tag) {
      case llvm::dwarf::DW_TAG_pointer_type: {
        // This is a pointer type - recurse to get the pointee
        auto base_llvm_type = ExtractPointeeTypeFromDIType(base_type, module, cache, visiting);
        if (base_llvm_type) {
          // Return the pointee type, not the pointer type
          result = base_llvm_type;
        }
        break;
      }
      case llvm::dwarf::DW_TAG_typedef:
      case llvm::dwarf::DW_TAG_const_type:
      case llvm::dwarf::DW_TAG_volatile_type:
      case llvm::dwarf::DW_TAG_restrict_type:
        // Unwrap these qualifiers
        result = ExtractPointeeTypeFromDIType(base_type, module, cache, visiting);
        break;
      default:
        break;
    }
  } else if (auto subroutine_type = llvm::dyn_cast<llvm::DISubroutineType>(ditype)) {
    // Function pointer type - would need to convert to llvm::FunctionType
    // This is complex, skip for now
    result = nullptr;
  }

  visiting.erase(ditype);

  if (result) {
    cache[ditype] = result;
  }

  return result;
}

// Pass 0: Custom Metadata Inference (Highest Priority)
// Reads type metadata attached by rellic's compilation pipeline
static void InferFromCustomMetadata(llvm::Module& module,
                                    rellic::PointerTypeInferenceAnalysis& analysis) {
  const llvm::StringRef metadata_kind = "rellic.pointee.type";
  unsigned md_kind_id = module.getContext().getMDKindID(metadata_kind);

  DLOG(INFO) << "Inferring types from custom metadata...";

  for (auto& func : module) {
    for (auto& bb : func) {
      for (auto& inst : bb) {
        // Check if this instruction has our custom metadata
        if (auto* md = inst.getMetadata(md_kind_id)) {
          // Metadata format: !{!"type_string", i64 size}
          if (auto* tuple = llvm::dyn_cast<llvm::MDTuple>(md)) {
            if (tuple->getNumOperands() >= 2) {
              // Extract type string
              if (auto* type_str_md = llvm::dyn_cast<llvm::MDString>(
                      tuple->getOperand(0))) {
                std::string type_str = type_str_md->getString().str();

                // Try to reconstruct the LLVM type from the string
                // For now, we'll do basic reconstruction
                llvm::Type* pointee_type = nullptr;

                // Handle basic integer types
                if (type_str == "i8") {
                  pointee_type = llvm::Type::getInt8Ty(module.getContext());
                } else if (type_str == "i16") {
                  pointee_type = llvm::Type::getInt16Ty(module.getContext());
                } else if (type_str == "i32") {
                  pointee_type = llvm::Type::getInt32Ty(module.getContext());
                } else if (type_str == "i64") {
                  pointee_type = llvm::Type::getInt64Ty(module.getContext());
                }
                // Handle float types
                else if (type_str == "float") {
                  pointee_type = llvm::Type::getFloatTy(module.getContext());
                } else if (type_str == "double") {
                  pointee_type = llvm::Type::getDoubleTy(module.getContext());
                }
                // Handle struct types - look up by name
                else if (type_str.find("struct.") == 0 ||
                         type_str.find("%struct.") == 0) {
                  // Extract struct name
                  std::string struct_name = type_str;
                  if (struct_name[0] == '%') {
                    struct_name = struct_name.substr(1);
                  }
                  pointee_type = llvm::StructType::getTypeByName(
                      module.getContext(), struct_name);
                }

                // If we successfully reconstructed the type, record it
                if (pointee_type) {
                  bool recorded = analysis.RecordInference(
                      &inst, pointee_type,
                      rellic::PointerTypeInferenceAnalysis::TypeSource::CUSTOM_METADATA);
                  if (recorded) {
                    DLOG(INFO) << "  Inferred from metadata: "
                               << rellic::LLVMThingToString(&inst) << " -> "
                               << type_str;
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

// Pass 1: Usage-Based Inference
static void InferFromUsage(llvm::Module& module,
                           rellic::PointerTypeInferenceAnalysis& analysis) {
  for (auto& func : module) {
    for (auto& bb : func) {
      for (auto& inst : bb) {
        // Load instructions: ptr points to loaded type
        if (auto load = llvm::dyn_cast<llvm::LoadInst>(&inst)) {
          auto ptr_operand = load->getPointerOperand();
          auto loaded_type = load->getType();
          analysis.RecordInference(
              ptr_operand, loaded_type,
              rellic::PointerTypeInferenceAnalysis::TypeSource::USAGE_ANALYSIS);
        }
        // Store instructions: ptr points to stored type
        else if (auto store = llvm::dyn_cast<llvm::StoreInst>(&inst)) {
          auto ptr_operand = store->getPointerOperand();
          auto value_type = store->getValueOperand()->getType();
          analysis.RecordInference(
              ptr_operand, value_type,
              rellic::PointerTypeInferenceAnalysis::TypeSource::USAGE_ANALYSIS);
        }
        // GEP instructions: base pointer has type from source element type
        else if (auto gep = llvm::dyn_cast<llvm::GetElementPtrInst>(&inst)) {
          auto ptr_operand = gep->getPointerOperand();
          auto source_elem_type = gep->getSourceElementType();
          analysis.RecordInference(
              ptr_operand, source_elem_type,
              rellic::PointerTypeInferenceAnalysis::TypeSource::USAGE_ANALYSIS);
        }
        // Alloca instructions: result points to allocated type
        else if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(&inst)) {
          auto allocated_type = alloca->getAllocatedType();
          analysis.RecordInference(
              alloca, allocated_type,
              rellic::PointerTypeInferenceAnalysis::TypeSource::USAGE_ANALYSIS);
        }
      }
    }
  }
}

// Pass 2: Debug Information Inference
static void InferFromDebugInfo(llvm::Module& module, rellic::DebugInfoCollector& dic,
                               rellic::PointerTypeInferenceAnalysis& analysis) {
  std::unordered_map<llvm::DIType*, llvm::Type*> ditype_cache;

  // Get the IR to DIType map from the debug info collector
  const auto& value_to_ditype = dic.GetIRToDITypeMap();

  for (const auto& [value, ditype] : value_to_ditype) {
    // Check if this is a pointer type
    if (auto derived_type = llvm::dyn_cast<llvm::DIDerivedType>(ditype)) {
      if (derived_type->getTag() == llvm::dwarf::DW_TAG_pointer_type) {
        // This is a pointer - extract the pointee type
        std::unordered_set<llvm::DIType*> visiting;
        auto pointee_type = ExtractPointeeTypeFromDIType(
            derived_type->getBaseType(), module, ditype_cache, visiting);

        if (pointee_type) {
          analysis.RecordInference(
              value, pointee_type,
              rellic::PointerTypeInferenceAnalysis::TypeSource::DEBUG_INFO);
        }
      }
    }
  }
}

// Pass 3: Propagation
static void PropagateTypes(llvm::Module& module,
                          rellic::PointerTypeInferenceAnalysis& analysis) {
  const int max_iterations = 10;
  bool changed = true;
  int iteration = 0;

  while (changed && iteration < max_iterations) {
    changed = false;
    iteration++;

    for (auto& func : module) {
      for (auto& bb : func) {
        for (auto& inst : bb) {
          // PHI nodes: if all operands have same pointee type, propagate
          if (auto phi = llvm::dyn_cast<llvm::PHINode>(&inst)) {
            if (!phi->getType()->isPointerTy()) {
              continue;
            }

            llvm::Type* common_pointee = nullptr;
            bool all_same = true;

            for (unsigned i = 0; i < phi->getNumIncomingValues(); i++) {
              auto incoming_value = phi->getIncomingValue(i);
              auto pointee_type = analysis.GetPointeeType(incoming_value);

              if (!pointee_type) {
                all_same = false;
                break;
              }

              if (!common_pointee) {
                common_pointee = pointee_type;
              } else if (common_pointee != pointee_type) {
                all_same = false;
                break;
              }
            }

            if (all_same && common_pointee) {
              bool recorded = analysis.RecordInference(
                  phi, common_pointee,
                  rellic::PointerTypeInferenceAnalysis::TypeSource::PROPAGATION);
              if (recorded) {
                changed = true;
              }
            }
          }
          // Select instructions: if both branches have same type, propagate
          else if (auto select = llvm::dyn_cast<llvm::SelectInst>(&inst)) {
            if (!select->getType()->isPointerTy()) {
              continue;
            }

            auto true_type = analysis.GetPointeeType(select->getTrueValue());
            auto false_type = analysis.GetPointeeType(select->getFalseValue());

            if (true_type && false_type && true_type == false_type) {
              bool recorded = analysis.RecordInference(
                  select, true_type,
                  rellic::PointerTypeInferenceAnalysis::TypeSource::PROPAGATION);
              if (recorded) {
                changed = true;
              }
            }
          }
          // Bitcast: propagate type from source to dest (if both pointers)
          else if (auto bitcast = llvm::dyn_cast<llvm::BitCastInst>(&inst)) {
            if (bitcast->getSrcTy()->isPointerTy() &&
                bitcast->getDestTy()->isPointerTy()) {
              auto src_pointee = analysis.GetPointeeType(bitcast->getOperand(0));
              if (src_pointee) {
                bool recorded = analysis.RecordInference(
                    bitcast, src_pointee,
                    rellic::PointerTypeInferenceAnalysis::TypeSource::PROPAGATION);
                if (recorded) {
                  changed = true;
                }
              }
            }
          }
        }
      }
    }
  }

  DLOG(INFO) << "Type propagation converged after " << iteration
             << " iterations";
}

// Main entry point
void rellic::InferPointerTypes(llvm::Module& module, rellic::DebugInfoCollector& dic,
                                rellic::PointerTypeInferenceAnalysis& analysis) {
  DLOG(INFO) << "Starting pointer type inference...";

  // Pass 0: Custom metadata (highest priority - 200)
  InferFromCustomMetadata(module, analysis);

  // Pass 1: Usage-based inference (priority 100)
  InferFromUsage(module, analysis);

  // Pass 2: Debug information inference (priority 60)
  InferFromDebugInfo(module, dic, analysis);

  // Pass 3: Propagation (priority 30)
  PropagateTypes(module, analysis);

  // Log statistics
  auto stats = analysis.GetStatistics();
  DLOG(INFO) << "Pointer type inference complete:";
  DLOG(INFO) << "  Total inferences: " << stats.total_inferences;
  DLOG(INFO) << "  From custom metadata: " << stats.custom_metadata;
  DLOG(INFO) << "  From usage: " << stats.usage_based;
  DLOG(INFO) << "  From debug info: " << stats.debug_info;
  DLOG(INFO) << "  From propagation: " << stats.propagation;
  DLOG(INFO) << "  Conflicts resolved: " << stats.conflicts_resolved;
}
