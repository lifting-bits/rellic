/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/RecursiveASTVisitor.h>

#include "rellic/AST/Util.h"

namespace rellic {

template <typename Derived>
class TransformVisitor : public clang::RecursiveASTVisitor<Derived> {
 protected:
  StmtMap substitutions;
  bool changed;

 public:
  TransformVisitor() : changed(false) {}

  virtual bool shouldTraversePostOrder() { return true; }

  void Initialize() {
    changed = false;
    substitutions.clear();
  }

  bool VisitFunctionDecl(clang::FunctionDecl *fdecl) {
    // DLOG(INFO) << "VisitFunctionDecl";
    if (auto body = fdecl->getBody()) {
      auto iter = substitutions.find(body);
      if (iter != substitutions.end()) {
        fdecl->setBody(iter->second);
        changed = true;
      }
    }
    return true;
  }

  bool VisitStmt(clang::Stmt *stmt) {
    // DLOG(INFO) << "VisitStmt";
    changed |= ReplaceChildren(stmt, substitutions);
    return true;
  }
};

}  // namespace rellic
