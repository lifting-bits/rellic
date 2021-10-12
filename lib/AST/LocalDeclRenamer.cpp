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

    decl->setDeclName(ast.CreateIdentifier(name->second));
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
