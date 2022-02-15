/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/NestedCondProp.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/Util.h"

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

NestedCondProp::NestedCondProp(StmtToIRMap &provenance, clang::ASTUnit &unit)
    : ModulePass(NestedCondProp::ID),
      TransformVisitor<NestedCondProp>(provenance),
      ast(unit),
      ast_ctx(&unit.getASTContext()),
      z3_ctx(new z3::context()),
      z3_gen(new rellic::Z3ConvVisitor(unit, z3_ctx.get())) {}

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
          parent_conds[child] = ast.CreateLNot(cond);
        }
      } else if (auto elif = clang::dyn_cast<clang::IfStmt>(stmt_else)) {
        parent_conds[elif] = ast.CreateLNot(cond);
      } else {
        LOG(FATAL)
            << "Else branch must be a clang::CompoundStmt or clang::IfStmt!";
      }
    }
  }
  // Retrieve a parent `clang::IfStmt` condition
  // and remove it from `cond` if it's present.
  auto iter = parent_conds.find(ifstmt);
  if (iter != parent_conds.end()) {
    auto child_expr{ifstmt->getCond()};
    auto parent_expr{iter->second};
    changed = Replace(*ast_ctx, /* from */ parent_expr,
                      /* to */ ast.CreateTrue(), /* in */
                      &child_expr);
    ifstmt->setCond(child_expr);
  }
  return true;
}

bool NestedCondProp::runOnModule(llvm::Module &module) {
  LOG(INFO) << "Propagating nested conditions";
  Initialize();
  TraverseDecl(ast_ctx->getTranslationUnitDecl());
  return changed;
}

}  // namespace rellic