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

#include "rellic/AST/NestedScopeCombiner.h"
#include "rellic/AST/Util.h"

namespace rellic {

char NestedScopeCombiner::ID = 0;

NestedScopeCombiner::NestedScopeCombiner(clang::ASTContext &ctx,
                                         rellic::IRToASTVisitor &ast_gen)
    : ModulePass(NestedScopeCombiner::ID),
      ast_ctx(&ctx),
      ast_gen(&ast_gen) {}

bool NestedScopeCombiner::VisitIfStmt(clang::IfStmt *ifstmt) {
  // DLOG(INFO) << "VisitIfStmt";
  // Determine whether `cond` is a constant expression that is always true and
  // `ifstmt` should be replaced by `then` in it's parent nodes.
  llvm::APSInt val;
  bool is_const = ifstmt->getCond()->isIntegerConstantExpr(val, *ast_ctx);
  if (is_const && val.getBoolValue()) {
    substitutions[ifstmt] = ifstmt->getThen();
  }
  return true;
}

bool NestedScopeCombiner::VisitCompoundStmt(clang::CompoundStmt *compound) {
  // DLOG(INFO) << "VisitCompoundStmt";
  bool has_compound = false;
  std::vector<clang::Stmt *> new_body;
  for (auto stmt : compound->body()) {
    if (auto child = clang::dyn_cast<clang::CompoundStmt>(stmt)) {
      new_body.insert(new_body.end(), child->body_begin(), child->body_end());
      has_compound = true;
    } else {
      new_body.push_back(stmt);
    }
  }
  
  if (has_compound) {
    substitutions[compound] = CreateCompoundStmt(*ast_ctx, new_body);
  }
  
  return true;
}

bool NestedScopeCombiner::runOnModule(llvm::Module &module) {
  LOG(INFO) << "Combining nested scopes";
  Initialize();
  TraverseDecl(ast_ctx->getTranslationUnitDecl());
  return changed;
}

llvm::ModulePass *createNestedScopeCombinerPass(clang::ASTContext &ctx,
                                                rellic::IRToASTVisitor &gen) {
  return new NestedScopeCombiner(ctx, gen);
}
}  // namespace rellic