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

#include "rellic/AST/DeadStmtElim.h"
#include "rellic/AST/Util.h"

namespace rellic {

char DeadStmtElim::ID = 0;

DeadStmtElim::DeadStmtElim(clang::ASTContext &ctx,
                           rellic::IRToASTVisitor &ast_gen)
    : ModulePass(DeadStmtElim::ID),
      ast_ctx(&ctx),
      ast_gen(&ast_gen) {}

bool DeadStmtElim::VisitIfStmt(clang::IfStmt *ifstmt) {
  // DLOG(INFO) << "VisitIfStmt";
  llvm::APSInt val;
  bool is_const = ifstmt->getCond()->isIntegerConstantExpr(val, *ast_ctx);
  auto compound = clang::dyn_cast<clang::CompoundStmt>(ifstmt->getThen());
  bool is_empty = compound ? compound->body_empty() : false;
  if ((is_const && !val.getBoolValue()) || is_empty) {
    substitutions[ifstmt] = nullptr;
  }
  return true;
}

bool DeadStmtElim::VisitCompoundStmt(clang::CompoundStmt *compound) {
  // DLOG(INFO) << "VisitCompoundStmt";
  std::vector<clang::Stmt *> new_body;
  for (auto stmt : compound->body()) {
    // Filter out nullptr statements
    if (!stmt) {
      continue;
    }
    // Add only necessary statements
    if (auto expr = clang::dyn_cast<clang::Expr>(stmt)) {
      if (expr->HasSideEffects(*ast_ctx)) {
        new_body.push_back(stmt);
      }
    } else {
      new_body.push_back(stmt);
    }
  }
  // Create the a new compound
  if (changed || new_body.size() < compound->size()) {
    substitutions[compound] = CreateCompoundStmt(*ast_ctx, new_body);
  }
  return true;
}

bool DeadStmtElim::runOnModule(llvm::Module &module) {
  LOG(INFO) << "Eliminating dead statements";
  Initialize();
  TraverseDecl(ast_ctx->getTranslationUnitDecl());
  return changed;
}

llvm::ModulePass *createDeadStmtElimPass(clang::ASTContext &ctx,
                                         rellic::IRToASTVisitor &gen) {
  return new DeadStmtElim(ctx, gen);
}
}  // namespace rellic