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

#include <z3++.h>

#include <clang/AST/RecursiveASTVisitor.h>

#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/TransformVisitor.h"
#include "rellic/AST/Z3ConvVisitor.h"

namespace rellic {

class NestedCondProp : public llvm::ModulePass,
                       public TransformVisitor<NestedCondProp> {
 private:
  clang::ASTContext *ast_ctx;
  rellic::IRToASTVisitor *ast_gen;
  std::unique_ptr<z3::context> z3_ctx;
  std::unique_ptr<rellic::Z3ConvVisitor> z3_gen;

  std::unordered_map<clang::IfStmt *, clang::Expr *> parent_conds;

 public:
  static char ID;

  bool shouldTraversePostOrder() { return false; }

  NestedCondProp(clang::ASTContext &ctx, rellic::IRToASTVisitor &ast_gen);

  bool VisitIfStmt(clang::IfStmt *stmt);

  bool runOnModule(llvm::Module &module) override;
};

llvm::ModulePass *createNestedCondPropPass(clang::ASTContext &ctx,
                                           rellic::IRToASTVisitor &ast_gen);
}  // namespace rellic

namespace llvm {
void initializeNestedCondPropPass(PassRegistry &);
}
