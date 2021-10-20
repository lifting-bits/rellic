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

#include <ios>

#include "rellic/AST/Compat/Stmt.h"

namespace rellic {

char LocalDeclRenamer::ID = 0;

LocalDeclRenamer::LocalDeclRenamer(clang::ASTUnit &unit, IRToNameMap &names,
                                   IRToValDeclMap &decls)
    : ModulePass(LocalDeclRenamer::ID),
      ast(unit),
      ast_ctx(&unit.getASTContext()),
      seen_names(1),
      names(names),
      inv_decl(decls) {}

bool LocalDeclRenamer::isNameVisible(const std::string &name) {
  for (auto &scope : seen_names) {
    if (scope.find(name) != scope.end()) {
      return true;
    }
  }
  return false;
}

bool LocalDeclRenamer::VisitVarDecl(clang::VarDecl *decl) {
  if (renamed_decls.find(decl) != renamed_decls.end()) {
    return true;
  }
  renamed_decls.insert(decl);

  auto val{decls.find(decl)};
  if (val == decls.end()) {
    return true;
  }

  auto name{names.find(val->second)};
  if (name == names.end()) {
    seen_names.back().insert(decl->getName().str());
    return true;
  }

  if (!isNameVisible(name->second)) {
    seen_names.back().insert(name->second);
    decl->setDeclName(ast.CreateIdentifier(name->second));
  } else {
    // Append the automatically-generated name to the debug-info name in order
    // to avoid any lexical scoping issue
    // TODO(frabert): Recover proper lexical scoping from debug info metadata
    auto old_name{decl->getName().str()};
    auto new_name{name->second + "_" + old_name};
    decl->setDeclName(ast.CreateIdentifier(new_name));
    seen_names.back().insert(new_name);
  }

  return true;
}

bool LocalDeclRenamer::TraverseFunctionDecl(clang::FunctionDecl *decl) {
  std::unordered_set<std::string> scope;
  for (auto param : decl->parameters()) {
    scope.insert(param->getName().str());
  }
  seen_names.push_back(scope);
  RecursiveASTVisitor<LocalDeclRenamer>::TraverseFunctionDecl(decl);
  seen_names.pop_back();
  return true;
}

bool LocalDeclRenamer::shouldTraversePostOrder() { return false; }

bool LocalDeclRenamer::runOnModule(llvm::Module &module) {
  Initialize();
  for (auto &pair : inv_decl) {
    decls[pair.second] = pair.first;
  }
  TraverseDecl(ast_ctx->getTranslationUnitDecl());
  return changed;
}

}  // namespace rellic
