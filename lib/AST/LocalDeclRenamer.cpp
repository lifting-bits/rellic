/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */
#include "rellic/AST/LocalDeclRenamer.h"

#include <clang/AST/Decl.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/Support/Casting.h>

#include "rellic/AST/Compat/Stmt.h"

namespace rellic {

char LocalDeclRenamer::ID = 0;

LocalDeclRenamer::LocalDeclRenamer(clang::ASTUnit &unit, IRToNameMap &names_,
                                   ValDeclToIRMap &decls_)
    : ModulePass(LocalDeclRenamer::ID),
      ast(unit),
      ast_ctx(&unit.getASTContext()),
      decls(decls_),
      names(names_) {}

bool LocalDeclRenamer::VisitDeclStmt(clang::DeclStmt *stmt) {
  if (auto *decl = clang::cast<clang::VarDecl>(stmt->getSingleDecl())) {
    auto val = decls.find(decl);
    if (val == decls.end()) {
      return true;
    }

    auto name = names.find(val->second);
    if (name == names.end()) {
      return true;
    }

    auto *old_decl = clang::cast<clang::VarDecl>(decl);
    auto *new_decl = ast.CreateVarDecl(old_decl->getDeclContext(),
                                       old_decl->getType(), name->second);
    old_to_new[old_decl] = new_decl;
    substitutions[stmt] = ast.CreateDeclStmt(new_decl);
  }

  return true;
}

bool LocalDeclRenamer::VisitDeclRefExpr(clang::DeclRefExpr *expr) {
  auto new_decl = old_to_new.find(expr->getDecl());
  if (new_decl != old_to_new.end()) {
    expr->setDecl(new_decl->second);
  }
  return true;
}

bool LocalDeclRenamer::runOnModule(llvm::Module &module) {
  LOG(INFO) << "Renaming local declarations";
  Initialize();
  TraverseDecl(ast_ctx->getTranslationUnitDecl());
  return changed;
}

}  // namespace rellic
