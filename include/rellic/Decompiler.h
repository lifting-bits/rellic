/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/Frontend/ASTUnit.h>
#include <llvm/IR/Module.h>

#include <memory>
#include <unordered_map>

#include "Result.h"

namespace rellic {
struct DecompilationOptions {
  bool lower_switches = false;
  bool remove_phi_nodes = false;
  bool disable_z3 = false;
};

struct DecompilationResult {
  using StmtToIRMap =
      std::unordered_map<const clang::Stmt*, const llvm::Value*>;
  using DeclToIRMap =
      std::unordered_map<const clang::ValueDecl*, const llvm::Value*>;
  using IRToStmtMap =
      std::unordered_map<const llvm::Value*, const clang::Stmt*>;
  using IRToDeclMap =
      std::unordered_map<const llvm::Value*, const clang::ValueDecl*>;

  std::unique_ptr<llvm::Module> module;
  std::unique_ptr<clang::ASTUnit> ast;
  StmtToIRMap stmt_provenance_map;
  IRToStmtMap value_to_stmt_map;
  DeclToIRMap decl_provenance_map;
  IRToDeclMap value_to_decl_map;
};

struct DecompilationError {
  std::unique_ptr<llvm::Module> module;
  std::unique_ptr<clang::ASTUnit> ast;
  std::string message;
};

Result<DecompilationResult, DecompilationError> Decompile(
    std::unique_ptr<llvm::Module> module, DecompilationOptions options = {});
}  // namespace rellic
