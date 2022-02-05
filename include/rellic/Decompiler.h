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
  bool dead_stmt_elimination = true;
  struct {
    bool expression_normalize = true;
    bool z3_cond_simplify = true;
    std::vector<std::string> z3_tactics{"aig", "simplify"};
    bool nested_cond_propagate = true;
    bool nested_scope_combine = true;
    bool cond_base_refine = true;
    bool reach_based_refine = true;
  } condition_based_refinement;
  struct {
    bool expression_normalize = true;
    bool loop_refine = true;
    bool nested_scope_combine = true;
  } loop_refinement;
  struct {
    bool expression_normalize = true;
    bool z3_cond_simplify = true;
    std::vector<std::string> z3_tactics{"aig", "simplify",
                                        "propagate-bv-bounds", "ctx-simplify"};
    bool nested_cond_propagate = true;
    bool nested_scope_combine = true;
  } scope_refinement;
  bool expression_normalize = true;
  bool expression_combine = true;
};

struct DecompilationResult {
  using StmtToIRMap =
      std::unordered_multimap<const clang::Stmt*, const llvm::Value*>;
  using DeclToIRMap =
      std::unordered_map<const clang::ValueDecl*, const llvm::Value*>;
  using TypeDeclToIRMap =
      std::unordered_map<const clang::TypeDecl*, const llvm::Type*>;
  using IRToStmtMap =
      std::unordered_multimap<const llvm::Value*, const clang::Stmt*>;
  using IRToDeclMap =
      std::unordered_map<const llvm::Value*, const clang::ValueDecl*>;
  using IRToTypeDeclMap =
      std::unordered_map<const llvm::Type*, const clang::TypeDecl*>;

  std::unique_ptr<llvm::Module> module;
  std::unique_ptr<clang::ASTUnit> ast;
  StmtToIRMap stmt_provenance_map;
  IRToStmtMap value_to_stmt_map;
  DeclToIRMap decl_provenance_map;
  IRToDeclMap value_to_decl_map;
  TypeDeclToIRMap type_provenance_map;
  IRToTypeDeclMap type_to_decl_map;
};

struct DecompilationError {
  std::unique_ptr<llvm::Module> module;
  std::unique_ptr<clang::ASTUnit> ast;
  std::string message;
};

Result<DecompilationResult, DecompilationError> Decompile(
    std::unique_ptr<llvm::Module> module, DecompilationOptions options = {});
}  // namespace rellic
