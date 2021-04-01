/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <llvm/Analysis/Passes.h>
#include <llvm/Analysis/RegionInfo.h>
#include <llvm/IR/Module.h>

#include <unordered_set>

#include "rellic/AST/IRToASTVisitor.h"

namespace rellic {

class GenerateAST : public llvm::ModulePass {
 private:
  clang::ASTContext *ast_ctx;
  rellic::IRToASTVisitor *ast_gen;
  rellic::ASTBuilder ast;
  
  std::unordered_map<llvm::BasicBlock *, clang::Expr *> reaching_conds;
  std::unordered_map<llvm::BasicBlock *, clang::IfStmt *> block_stmts;
  std::unordered_map<llvm::Region *, clang::CompoundStmt *> region_stmts;

  llvm::DominatorTree *domtree;
  llvm::RegionInfo *regions;
  llvm::LoopInfo *loops;

  std::vector<llvm::BasicBlock *> rpo_walk;

  clang::Expr *CreateEdgeCond(llvm::BasicBlock *from, llvm::BasicBlock *to);
  clang::Expr *GetOrCreateReachingCond(llvm::BasicBlock *block);
  std::vector<clang::Stmt *> CreateBasicBlockStmts(llvm::BasicBlock *block);
  std::vector<clang::Stmt *> CreateRegionStmts(llvm::Region *region);

  using BBSet = std::unordered_set<llvm::BasicBlock *>;

  void RefineLoopSuccessors(llvm::Loop *loop, BBSet &members,
                            BBSet &successors);

  clang::CompoundStmt *StructureAcyclicRegion(llvm::Region *region);
  clang::CompoundStmt *StructureCyclicRegion(llvm::Region *region);
  clang::CompoundStmt *StructureRegion(llvm::Region *region);

 public:
  static char ID;

  GenerateAST(clang::ASTContext &ctx, rellic::IRToASTVisitor &gen);

  void getAnalysisUsage(llvm::AnalysisUsage &usage) const override;
  bool runOnModule(llvm::Module &module) override;
};

llvm::ModulePass *createGenerateASTPass(clang::ASTContext &ctx,
                                        rellic::IRToASTVisitor &gen);
}  // namespace rellic

namespace llvm {
void initializeGenerateASTPass(PassRegistry &);
}
