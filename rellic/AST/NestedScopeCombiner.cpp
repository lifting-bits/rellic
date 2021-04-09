/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/NestedScopeCombiner.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/Util.h"

namespace rellic {

char NestedScopeCombiner::ID = 0;

NestedScopeCombiner::NestedScopeCombiner(clang::ASTUnit &unit,
                                         rellic::IRToASTVisitor &ast_gen)
    : ModulePass(NestedScopeCombiner::ID),
      ast_ctx(&unit.getASTContext()),
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

llvm::ModulePass *createNestedScopeCombinerPass(clang::ASTUnit &unit,
                                                rellic::IRToASTVisitor &gen) {
  return new NestedScopeCombiner(unit, gen);
}
}  // namespace rellic