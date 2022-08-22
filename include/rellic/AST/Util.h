/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/DeclBase.h>
#include <clang/AST/Stmt.h>
#include <clang/Frontend/ASTUnit.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <z3++.h>

#include <unordered_map>

#include "rellic/AST/ASTBuilder.h"

namespace rellic {

unsigned GetHash(clang::ASTContext &ctx, clang::Stmt *stmt);
bool IsEquivalent(clang::Expr *a, clang::Expr *b);
template <typename TFrom, typename TIn>
bool Replace(TFrom *from, clang::Expr *to, TIn **in) {
  auto from_expr{clang::cast<clang::Expr>(from)};
  auto in_expr{clang::cast<clang::Expr>(*in)};
  if (IsEquivalent(in_expr, from_expr)) {
    *in = to;
    return true;
  } else {
    bool changed{false};
    for (auto child{(*in)->child_begin()}; child != (*in)->child_end();
         ++child) {
      changed |= Replace(from_expr, to, &*child);
    }
    return changed;
  }
}

template <typename T>
size_t GetNumDecls(clang::DeclContext *decl_ctx) {
  size_t result = 0;
  for (auto decl : decl_ctx->decls()) {
    if (clang::isa<T>(decl)) {
      ++result;
    }
  }
  return result;
}

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
struct DecompilationContext {
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
  std::map<BrEdge, unsigned> z3_br_edges;

  std::unordered_map<llvm::SwitchInst *, unsigned> z3_sw_vars;
  std::unordered_map<unsigned, llvm::SwitchInst *> z3_sw_vars_inv;
  std::map<SwEdge, unsigned> z3_sw_edges;

  std::map<BBEdge, unsigned> z3_edges;
  std::unordered_map<llvm::BasicBlock *, unsigned> reaching_conds;

  size_t num_literal_structs = 0;
  size_t num_declared_structs = 0;
};

template <typename TKey1, typename TKey2, typename TKey3, typename TValue>
void CopyProvenance(TKey1 *from, TKey2 *to,
                    std::unordered_map<TKey3 *, TValue *> &map) {
  map[to] = map[from];
}

clang::Expr *Clone(clang::ASTUnit &unit, clang::Expr *stmt,
                   ExprToUseMap &provenance);

std::string ClangThingToString(const clang::Stmt *stmt);

z3::goal ApplyTactic(const z3::tactic &tactic, z3::expr expr);

bool Prove(z3::expr expr);

z3::expr HeavySimplify(z3::expr expr);
z3::expr_vector Clone(z3::expr_vector &vec);

// Tries to keep each subformula sorted by its id so that they don't get
// shuffled around by simplification
z3::expr Sort(z3::expr expr);
}  // namespace rellic