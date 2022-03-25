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

#include <unordered_set>

#include "rellic/AST/IRToASTVisitor.h"

namespace rellic {

class GenerateAST : public llvm::AnalysisInfoMixin<GenerateAST> {
 private:
  friend llvm::AnalysisInfoMixin<GenerateAST>;
  static llvm::AnalysisKey Key;
  clang::ASTUnit &unit;
  clang::ASTContext *ast_ctx;
  rellic::IRToASTVisitor ast_gen;
  rellic::ASTBuilder ast;

  StmtToIRMap &provenance;

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
  using Result = llvm::PreservedAnalyses;
  GenerateAST(StmtToIRMap &provenance, clang::ASTUnit &unit,
              IRToTypeDeclMap &type_decls, IRToValDeclMap &value_decls,
              ArgToTempMap &temp_decls, ExprToUseMap &use_provenance);

  StmtToIRMap &GetStmtToIRMap() { return provenance; }
  ExprToUseMap &GetExprToUseMap() { return ast_gen.GetExprToUseMap(); }
  IRToValDeclMap &GetIRToValDeclMap() { return ast_gen.GetIRToValDeclMap(); }
  IRToTypeDeclMap &GetIRToTypeDeclMap() { return ast_gen.GetIRToTypeDeclMap(); }

  Result run(llvm::Function &F, llvm::FunctionAnalysisManager &FAM);
  Result run(llvm::Module &M, llvm::ModuleAnalysisManager &MAM);

  static void run(llvm::Module &M, StmtToIRMap &provenance,
                  clang::ASTUnit &unit, IRToTypeDeclMap &type_decls,
                  IRToValDeclMap &value_decls, ArgToTempMap &temp_decls,
                  ExprToUseMap &use_provenance);
};

}  // namespace rellic
