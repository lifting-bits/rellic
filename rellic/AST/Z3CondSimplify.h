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

#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/TransformVisitor.h"
#include "rellic/AST/Z3ConvVisitor.h"

namespace rellic {

class Z3CondSimplify : public llvm::ModulePass,
                       public TransformVisitor<Z3CondSimplify> {
 private:
  clang::ASTContext *ast_ctx;
  rellic::IRToASTVisitor *ast_gen;
  std::unique_ptr<z3::context> z3_ctx;
  std::unique_ptr<rellic::Z3ConvVisitor> z3_gen;

  z3::tactic z3_simplifier;

  clang::Expr *SimplifyCExpr(clang::Expr *c_expr);

 public:
  static char ID;

  Z3CondSimplify(clang::ASTContext &ctx, rellic::IRToASTVisitor &ast_gen);

  z3::context &GetZ3Context() { return *z3_ctx; }
  
  void SetZ3Simplifier(z3::tactic tactic) { z3_simplifier = tactic; };

  bool VisitIfStmt(clang::IfStmt *stmt);
  bool VisitWhileStmt(clang::WhileStmt *loop);
  bool VisitDoStmt(clang::DoStmt *loop);

  bool runOnModule(llvm::Module &module) override;
};

llvm::ModulePass *createZ3CondSimplifyPass(clang::ASTContext &ctx,
                                           rellic::IRToASTVisitor &gen);
}  // namespace rellic

namespace llvm {
void initializeZ3CondSimplifyPass(PassRegistry &);
}
