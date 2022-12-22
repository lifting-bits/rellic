/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/Frontend/ASTUnit.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <z3++.h>

#include <unordered_map>

#include "rellic/AST/ASTBuilder.h"
#include "rellic/AST/TypeProvider.h"
#include "rellic/AST/VariableProvider.h"

namespace rellic {

struct DecompilationContext {
  using StmtToIRMap = std::unordered_map<clang::Stmt *, llvm::Value *>;
  using ExprToUseMap = std::unordered_map<clang::Expr *, llvm::Use *>;
  using IRToTypeDeclMap = std::unordered_map<llvm::Type *, clang::TypeDecl *>;
  using IRToValDeclMap = std::unordered_map<llvm::Value *, clang::ValueDecl *>;
  using IRToStmtMap = std::unordered_map<llvm::Value *, clang::Stmt *>;
  using ArgToTempMap = std::unordered_map<llvm::Argument *, clang::VarDecl *>;
  using BlockToUsesMap =
      std::unordered_map<llvm::BasicBlock *, std::vector<llvm::Use *>>;
  using Z3CondMap = std::unordered_map<clang::Stmt *, unsigned>;

  using BBEdge = std::pair<llvm::BasicBlock *, llvm::BasicBlock *>;
  using BrEdge = std::pair<llvm::BranchInst *, bool>;
  using SwEdge = std::pair<llvm::SwitchInst *, llvm::ConstantInt *>;

  DecompilationContext(clang::ASTUnit &ast_unit);

  clang::ASTUnit &ast_unit;
  clang::ASTContext &ast_ctx;
  ASTBuilder ast;

  std::unique_ptr<TypeProviderCombiner> type_provider;
  std::unique_ptr<VariableProviderCombiner> var_provider;

  StmtToIRMap stmt_provenance;
  ExprToUseMap use_provenance;
  IRToTypeDeclMap type_decls;
  IRToValDeclMap value_decls;
  ArgToTempMap temp_decls;
  BlockToUsesMap outgoing_uses;
  z3::context z3_ctx;
  z3::expr_vector z3_exprs{z3_ctx};
  Z3CondMap conds;

  clang::Expr *marker_expr;

  std::unordered_map<unsigned, BrEdge> z3_br_edges_inv;

  // Pairs do not have a std::hash specialization so we can't use unordered maps
  // here. If this turns out to be a performance issue, investigate adding hash
  // specializations for these specifically
  std::map<BrEdge, unsigned> z3_br_edges;

  std::unordered_map<llvm::SwitchInst *, unsigned> z3_sw_vars;
  std::unordered_map<unsigned, llvm::SwitchInst *> z3_sw_vars_inv;
  std::map<SwEdge, unsigned> z3_sw_edges;

  std::map<BBEdge, unsigned> z3_edges;
  std::unordered_map<llvm::BasicBlock *, unsigned> reaching_conds;

  size_t num_literal_structs = 0;
  size_t num_declared_structs = 0;

  // Inserts an expression into z3_exprs and returns its index
  unsigned InsertZExpr(const z3::expr &e);

  clang::QualType GetQualType(llvm::Type *type);
};

}  // namespace rellic