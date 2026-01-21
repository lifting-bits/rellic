/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>

#include <unordered_map>

namespace rellic {

class DebugInfoCollector;

// Tracks inferred pointer element types for opaque pointers
class PointerTypeInferenceAnalysis {
 public:
  // Priority/confidence for each inference (higher = better)
  enum class TypeSource {
    UNKNOWN = 0,
    PROPAGATION = 30,       // Propagated through PHI/Select
    DEBUG_INFO = 60,        // From DWARF debug metadata
    USAGE_ANALYSIS = 100,   // From load/store/GEP usage (highest priority)
  };

  PointerTypeInferenceAnalysis() = default;

  // Get the inferred pointee type for a pointer value
  // Returns nullptr if no type was inferred
  llvm::Type* GetPointeeType(llvm::Value* ptr_value) const;

  // Record a type inference for a pointer value
  // Returns true if the inference was recorded (either new or higher priority)
  // Returns false if a higher/equal priority inference already exists
  bool RecordInference(llvm::Value* ptr_value, llvm::Type* pointee_type,
                       TypeSource source);

  // Get the source of the inference for a pointer value
  TypeSource GetInferenceSource(llvm::Value* ptr_value) const;

  // Get statistics about inferences
  struct Statistics {
    size_t total_inferences{0};
    size_t usage_based{0};
    size_t debug_info{0};
    size_t propagation{0};
    size_t conflicts_resolved{0};
  };
  Statistics GetStatistics() const;

 private:
  // Maps LLVM pointer values to their inferred pointee types
  std::unordered_map<llvm::Value*, llvm::Type*> value_to_pointee_type_;

  // Priority/confidence for each inference
  std::unordered_map<llvm::Value*, TypeSource> inference_source_;

  // Statistics tracking
  mutable Statistics stats_;
};

// Main entry point: Infer pointer types from usage and debug info
void InferPointerTypes(llvm::Module& module, DebugInfoCollector& dic,
                       PointerTypeInferenceAnalysis& analysis);

}  // namespace rellic
