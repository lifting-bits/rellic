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

#include "rellic/AST/NestedCondProp.h"
#include "rellic/AST/Util.h"
#include "rellic/AST/Z3ConvVisitor.h"

namespace rellic {

namespace {

static std::vector<clang::IfStmt *> GetIfStmts(clang::CompoundStmt *compound) {
  std::vector<clang::IfStmt *> result;
  for (auto stmt : compound->body()) {
    if (auto ifstmt = clang::dyn_cast<clang::IfStmt>(stmt)) {
      result.push_back(ifstmt);
    }
  }
  return result;
}

}  // namespace

char NestedCondProp::ID = 0;

NestedCondProp::NestedCondProp(clang::ASTContext &ctx,
                               rellic::IRToASTVisitor &ast_gen)
    : ModulePass(NestedCondProp::ID),
      ast_ctx(&ctx),
      ast_gen(&ast_gen),
      z3_ctx(new z3::context()),
      z3_gen(new rellic::Z3ConvVisitor(ast_ctx, z3_ctx.get())) {}

bool NestedCondProp::VisitIfStmt(clang::IfStmt *ifstmt) {
  // DLOG(INFO) << "VisitIfStmt";
  // Determine whether `cond` is a constant expression
  auto cond = ifstmt->getCond();
  if (!cond->isIntegerConstantExpr(*ast_ctx)) {
    auto stmt_then = ifstmt->getThen();
    auto stmt_else = ifstmt->getElse();
    // `cond` is not a constant expression and we propagate it
    // to `clang::IfStmt` nodes in it's `then` branch.
    if (auto comp = clang::dyn_cast<clang::CompoundStmt>(stmt_then)) {
      for (auto child : GetIfStmts(comp)) {
        parent_conds[child] = cond;
      }
    } else {
      LOG(FATAL) << "Then branch must be a clang::CompoundStmt!";
    }
    if (stmt_else) {
      if (auto comp = clang::dyn_cast<clang::CompoundStmt>(stmt_else)) {
        for (auto child : GetIfStmts(comp)) {
          parent_conds[child] = CreateNotExpr(*ast_ctx, cond);
        }
      } else {
        LOG(FATAL) << "Else branch must be a clang::CompoundStmt!";
      }
    }
  }
  // Retrieve a parent `clang::IfStmt` condition
  // and remove it from `cond` if it's present.
  auto iter = parent_conds.find(ifstmt);
  if (iter != parent_conds.end()) {
    auto child_expr = z3_gen->GetOrCreateZ3Expr(ifstmt->getCond()).simplify();
    auto parent_expr = z3_gen->GetOrCreateZ3Expr(iter->second).simplify();
    z3::expr_vector src(*z3_ctx);
    z3::expr_vector dst(*z3_ctx);
    src.push_back(parent_expr);
    dst.push_back(z3_ctx->bool_val(true));
    auto sub = child_expr.substitute(src, dst).simplify();
    if (!z3::eq(child_expr, sub)) {
      ifstmt->setCond(z3_gen->GetOrCreateCExpr(sub));
      changed = true;
    }
  }
  return true;
}

bool NestedCondProp::runOnModule(llvm::Module &module) {
  LOG(INFO) << "Propagating nested conditions";
  changed = false;
  parent_conds.clear();
  TraverseDecl(ast_ctx->getTranslationUnitDecl());
  return changed;
}

llvm::ModulePass *createNestedCondPropPass(clang::ASTContext &ctx,
                                           rellic::IRToASTVisitor &gen) {
  return new NestedCondProp(ctx, gen);
}
}  // namespace rellic