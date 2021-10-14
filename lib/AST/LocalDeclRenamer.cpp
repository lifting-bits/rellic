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
                                   IRToValDeclMap &decls_)
    : ModulePass(LocalDeclRenamer::ID),
      ast(unit),
      ast_ctx(&unit.getASTContext()),
      names(names_) {
  for (auto &pair : decls_) {
    decls[pair.second] = pair.first;
  }
}

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

    // Append the automatically-generated name to the debug-info name in order
    // to avoid any lexical scoping issue
    // FIXME: Recover proper lexical scoping from debug info metadata
    auto old_name = decl->getName().str();
    auto new_name = name->second + "_" + old_name;
    decl->setDeclName(ast.CreateIdentifier(new_name));
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
