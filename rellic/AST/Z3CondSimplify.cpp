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

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/Z3CondSimplify.h"

namespace rellic {

char Z3CondSimplify::ID = 0;

Z3CondSimplify::Z3CondSimplify(clang::ASTContext &ctx,
                               rellic::IRToASTVisitor &ast_gen)
    : ModulePass(Z3CondSimplify::ID),
      ast_ctx(&ctx),
      ast_gen(&ast_gen),
      z3_ctx(new z3::context()),
      z3_gen(new rellic::Z3ConvVisitor(ast_ctx, z3_ctx.get())),
      z3_simplifier(*z3_ctx, "simplify") {}

clang::Expr *Z3CondSimplify::SimplifyCExpr(clang::Expr *c_expr) {
  auto z3_expr = z3_gen->GetOrCreateZ3Expr(c_expr);
  z3::goal goal(*z3_ctx);
  goal.add(z3_expr);
  // Apply on `z3_simplifier` on condition
  auto app = z3_simplifier(goal);
  CHECK(app.size() == 1) << "Unexpected multiple goals in application!";
  auto z3_result = app[0].as_expr().simplify();
  return z3_gen->GetOrCreateCExpr(z3_result);
}

bool Z3CondSimplify::VisitIfStmt(clang::IfStmt *stmt) {
  stmt->setCond(SimplifyCExpr(stmt->getCond()));
  return true;
}

bool Z3CondSimplify::VisitWhileStmt(clang::WhileStmt *loop) {
  loop->setCond(SimplifyCExpr(loop->getCond()));
  return true;
}

bool Z3CondSimplify::VisitDoStmt(clang::DoStmt *loop) {
  loop->setCond(SimplifyCExpr(loop->getCond()));
  return true;
}

bool Z3CondSimplify::runOnModule(llvm::Module &module) {
  LOG(INFO) << "Simplifying conditions using Z3";
  Initialize();
  TraverseDecl(ast_ctx->getTranslationUnitDecl());
  return changed;
}

llvm::ModulePass *createZ3CondSimplifyPass(clang::ASTContext &ctx,
                                           rellic::IRToASTVisitor &gen) {
  return new Z3CondSimplify(ctx, gen);
}
}  // namespace rellic