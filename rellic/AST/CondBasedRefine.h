/*
 * Copyright (c) 2018 Trail of Bits, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <llvm/IR/Module.h>
#include <llvm/Pass.h>

#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/TransformVisitor.h"
#include "rellic/AST/Z3ConvVisitor.h"

namespace rellic {

class CondBasedRefine : public llvm::ModulePass,
                        public TransformVisitor<CondBasedRefine> {
 private:
  clang::ASTContext *ast_ctx;
  rellic::IRToASTVisitor *ast_gen;
  std::unique_ptr<z3::context> z3_ctx;
  std::unique_ptr<rellic::Z3ConvVisitor> z3_gen;

  z3::tactic z3_solver;

  z3::expr GetZ3Cond(clang::IfStmt *ifstmt);

  bool Prove(z3::expr expr);

  using IfStmtVec = std::vector<clang::IfStmt *>;

  void CreateIfThenElseStmts(IfStmtVec stmts);

 public:
  static char ID;

  CondBasedRefine(clang::ASTContext &ctx, rellic::IRToASTVisitor &ast_gen);

  bool VisitCompoundStmt(clang::CompoundStmt *compound);

  bool runOnModule(llvm::Module &module) override;
};

llvm::ModulePass *createCondBasedRefinePass(clang::ASTContext &ctx,
                                            rellic::IRToASTVisitor &ast_gen);
}  // namespace rellic

namespace llvm {
void initializeCondBasedRefinePass(PassRegistry &);
}
