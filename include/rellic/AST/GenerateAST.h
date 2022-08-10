/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <llvm/Analysis/RegionInfo.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/PassManager.h>
#include <z3++.h>

#include <map>
#include <unordered_set>

#include "rellic/AST/IRToASTVisitor.h"

namespace rellic {

class GenerateAST : public llvm::AnalysisInfoMixin<GenerateAST> {
 private:
  friend llvm::AnalysisInfoMixin<GenerateAST>;
  static llvm::AnalysisKey Key;

  constexpr static unsigned POISON_IDX = static_cast<unsigned>(-1);

  // Need to use `map` with these instead of `unordered_map`, because
  // `std::pair` doesn't have a default hash implementation
  clang::ASTUnit &unit;
  clang::ASTContext *ast_ctx;
  rellic::IRToASTVisitor ast_gen;
  rellic::ASTBuilder ast;
  Provenance &provenance;
  bool reaching_conds_changed{true};
  std::unordered_map<llvm::BasicBlock *, clang::IfStmt *> block_stmts;
  std::unordered_map<llvm::Region *, clang::CompoundStmt *> region_stmts;

  llvm::DominatorTree *domtree;
  llvm::RegionInfo *regions;
  llvm::LoopInfo *loops;

  std::vector<llvm::BasicBlock *> rpo_walk;

  unsigned GetOrCreateEdgeForBranch(llvm::BranchInst *inst, bool cond);
  unsigned GetOrCreateVarForSwitch(llvm::SwitchInst *inst);
  unsigned GetOrCreateEdgeForSwitch(llvm::SwitchInst *inst,
                                    llvm::ConstantInt *c);

  unsigned GetOrCreateEdgeCond(llvm::BasicBlock *from, llvm::BasicBlock *to);
  unsigned GetReachingCond(llvm::BasicBlock *block);
  void CreateReachingCond(llvm::BasicBlock *block);

  std::vector<clang::Stmt *> CreateBasicBlockStmts(llvm::BasicBlock *block);
  std::vector<clang::Stmt *> CreateRegionStmts(llvm::Region *region);

  using BBSet = std::unordered_set<llvm::BasicBlock *>;

  void RefineLoopSuccessors(llvm::Loop *loop, BBSet &members,
                            BBSet &successors);

  clang::CompoundStmt *StructureAcyclicRegion(llvm::Region *region);
  clang::CompoundStmt *StructureCyclicRegion(llvm::Region *region);
  clang::CompoundStmt *StructureSwitchRegion(llvm::Region *region);
  clang::CompoundStmt *StructureRegion(llvm::Region *region);

 public:
  using Result = llvm::PreservedAnalyses;
  GenerateAST(Provenance &provenance, clang::ASTUnit &unit);

  Result run(llvm::Function &F, llvm::FunctionAnalysisManager &FAM);
  Result run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM);

  static void run(llvm::Module &M, Provenance &provenance,
                  clang::ASTUnit &unit);
};

}  // namespace rellic
