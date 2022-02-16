/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/RecursiveASTVisitor.h>

#include <unordered_map>

#include "rellic/AST/IRToASTVisitor.h"

namespace rellic {

using StmtSubMap = std::unordered_map<clang::Stmt *, clang::Stmt *>;

template <typename Derived>
class TransformVisitor : public clang::RecursiveASTVisitor<Derived> {
 protected:
  StmtToIRMap &provenance;
  StmtSubMap substitutions;
  bool changed;

  void EraseProvenance(clang::Stmt *from) { provenance.erase(from); }

  void CopyProvenance(clang::Stmt *from, clang::Stmt *to) {
    ::rellic::CopyProvenance(from, to, provenance);
  }

  bool ReplaceChildren(clang::Stmt *stmt, StmtSubMap &repl_map) {
    auto change = false;
    for (auto c_it = stmt->child_begin(); c_it != stmt->child_end(); ++c_it) {
      auto s_it = repl_map.find(*c_it);
      if (s_it != repl_map.end()) {
        *c_it = s_it->second;
        CopyProvenance(s_it->first, s_it->second);
        change = true;
      }
    }
    return change;
  }

 public:
  TransformVisitor(StmtToIRMap &provenance)
      : provenance(provenance), changed(false) {}

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
