/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <memory>
#include <string>

#include "rellic/Result.h"

namespace llvm {
class Module;
}

namespace clang {
class ASTUnit;
}

namespace rellic {

/// Options for AST to IR compilation
struct CompilationOptions {
  /// Attach custom metadata encoding pointee types for opaque pointers
  bool emit_type_metadata = true;

  /// Emit DWARF debug information
  bool emit_debug_info = false;

  /// Target triple (empty = host default)
  std::string target_triple = "";

  /// Optimization level (0-3, like -O0 to -O3)
  unsigned optimization_level = 0;
};

/// Result of successful compilation
struct CompilationResult {
  /// Generated LLVM IR module with optional type metadata
  std::unique_ptr<llvm::Module> module;

  /// Original AST unit (ownership transferred back)
  std::unique_ptr<clang::ASTUnit> ast;
};

/// Error information from failed compilation
struct CompilationError {
  /// Original AST unit (ownership transferred back)
  std::unique_ptr<clang::ASTUnit> ast;

  /// Error message describing what went wrong
  std::string message;
};

/// Compile Clang AST to LLVM IR with optional type metadata preservation.
///
/// This function provides the inverse operation of decompilation, allowing
/// roundtrip conversions (IR → AST → IR → AST) with type fidelity.
///
/// When emit_type_metadata is enabled, custom LLVM metadata is attached to
/// pointer instructions encoding their pointee types. This metadata is read
/// by the decompilation pipeline's PointerTypeInference with highest priority.
///
/// Metadata format:
///   !rellic.pointee.type = !{!"<type_string>", i64 <size_bytes>}
///
/// Example usage:
/// \code
///   auto ast = clang::tooling::buildASTFromCode("int* p;", "test.c");
///   auto result = rellic::Compile(std::move(ast));
///   if (result.Succeeded()) {
///     auto value = result.TakeValue();
///     value.module->print(llvm::outs(), nullptr);
///   }
/// \endcode
///
/// @param ast_unit The Clang AST to compile (ownership transferred)
/// @param options Compilation options (defaults to type metadata enabled)
/// @return CompilationResult on success, CompilationError on failure
Result<CompilationResult, CompilationError> Compile(
    std::unique_ptr<clang::ASTUnit> ast_unit,
    CompilationOptions options = {});

}  // namespace rellic
