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
