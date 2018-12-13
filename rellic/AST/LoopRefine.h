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

#ifndef RELLIC_AST_LOOPREFINE_H_
#define RELLIC_AST_LOOPREFINE_H_

#include <llvm/IR/Module.h>

#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/TransformVisitor.h"
#include "rellic/AST/Util.h"

namespace rellic {

class LoopRefine : public llvm::ModulePass,
                   public TransformVisitor<LoopRefine> {
 private:
  clang::ASTContext *ast_ctx;
  rellic::IRToASTVisitor *ast_gen;

 public:
  static char ID;

  LoopRefine(clang::CompilerInstance &ins, rellic::IRToASTVisitor &ast_gen);

  bool VisitWhileStmt(clang::WhileStmt *loop);

  bool runOnModule(llvm::Module &module) override;
};

llvm::ModulePass *createLoopRefinePass(clang::CompilerInstance &ins,
                                       rellic::IRToASTVisitor &ast_gen);
}  // namespace rellic

namespace llvm {
void initializeLoopRefinePass(PassRegistry &);
}

#endif  // RELLIC_AST_LOOPREFINE_H_
