/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/Compat/Stmt.h"
#include "rellic/AST/NestedScopeCombine.h"

namespace rellic {

char NestedScopeCombine::ID = 0;

NestedScopeCombine::NestedScopeCombine(clang::ASTUnit &unit,
                                       rellic::IRToASTVisitor &ast_gen)
    : ModulePass(NestedScopeCombine::ID),
      ast(unit),
      ast_ctx(&unit.getASTContext()),
      ast_gen(&ast_gen) {}

bool NestedScopeCombine::VisitIfStmt(clang::IfStmt *ifstmt) {
  // DLOG(INFO) << "VisitIfStmt";
  // Determine whether `cond` is a constant expression that is always true and
  // `ifstmt` should be replaced by `then` in it's parent nodes.
  auto if_const_expr = rellic::GetIntegerConstantExprFromIf(ifstmt, *ast_ctx);
  bool is_const = if_const_expr.hasValue();
  if (is_const && if_const_expr->getBoolValue()) {
    substitutions[ifstmt] = ifstmt->getThen();
  }
  return true;
}

bool NestedScopeCombine::VisitCompoundStmt(clang::CompoundStmt *compound) {
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
    substitutions[compound] = ast.CreateCompoundStmt(new_body);
  }

  return true;
}

bool NestedScopeCombine::runOnModule(llvm::Module &module) {
  LOG(INFO) << "Combining nested scopes";
  Initialize();
  TraverseDecl(ast_ctx->getTranslationUnitDecl());
  return changed;
}

NestedScopeCombine *createNestedScopeCombinePass(clang::ASTUnit &unit,
                                                  rellic::IRToASTVisitor &gen) {
  return new NestedScopeCombine(unit, gen);
}
}  // namespace rellic
