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
};

struct DecompilationResult {
  using StmtToIRMap =
      std::unordered_map<const clang::Stmt*, const llvm::Value*>;
  using DeclToIRMap =
      std::unordered_map<const clang::ValueDecl*, const llvm::Value*>;
  using TypeDeclToIRMap =
      std::unordered_map<const clang::TypeDecl*, const llvm::Type*>;
  using ExprToUseMap = std::unordered_map<const clang::Expr*, const llvm::Use*>;
  using IRToStmtMap =
      std::unordered_map<const llvm::Value*, const clang::Stmt*>;
  using IRToDeclMap =
      std::unordered_map<const llvm::Value*, const clang::ValueDecl*>;
  using IRToTypeDeclMap =
      std::unordered_map<const llvm::Type*, const clang::TypeDecl*>;
  using UseToExprMap = std::unordered_map<const llvm::Use*, const clang::Expr*>;

  std::unique_ptr<llvm::Module> module;
  std::unique_ptr<clang::ASTUnit> ast;
  StmtToIRMap stmt_provenance_map;
  IRToStmtMap value_to_stmt_map;
  DeclToIRMap decl_provenance_map;
  IRToDeclMap value_to_decl_map;
  TypeDeclToIRMap type_provenance_map;
  IRToTypeDeclMap type_to_decl_map;
  ExprToUseMap expr_use_map;
  UseToExprMap use_expr_map;
};

struct DecompilationError {
  std::unique_ptr<llvm::Module> module;
  std::unique_ptr<clang::ASTUnit> ast;
  std::string message;
};

Result<DecompilationResult, DecompilationError> Decompile(
    std::unique_ptr<llvm::Module> module, DecompilationOptions options = {});
}  // namespace rellic
