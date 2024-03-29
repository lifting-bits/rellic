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

#include "rellic/AST/ASTPass.h"
#include "rellic/AST/Util.h"

namespace rellic {

using StmtSubMap = std::unordered_map<clang::Stmt *, clang::Stmt *>;

template <typename Derived>
class TransformVisitor : public ASTPass,
                         public clang::RecursiveASTVisitor<Derived> {
 protected:
  StmtSubMap substitutions;

  void CopyProvenance(clang::Stmt *from, clang::Stmt *to) {
    ::rellic::CopyProvenance(from, to, dec_ctx.stmt_provenance);
  }

  void CopyProvenance(clang::Expr *from, clang::Expr *to) {
    ::rellic::CopyProvenance(from, to, dec_ctx.use_provenance);
  }

  bool ReplaceChildren(clang::Stmt *stmt, StmtSubMap &repl_map) {
    auto change = false;
    for (auto c_it = stmt->child_begin(); c_it != stmt->child_end(); ++c_it) {
      auto s_it = repl_map.find(*c_it);
      if (s_it != repl_map.end()) {
        *c_it = s_it->second;
        CopyProvenance(s_it->first, s_it->second);
        if (clang::isa<clang::Expr>(s_it->first) &&
            clang::isa<clang::Expr>(s_it->second)) {
          auto from_expr{clang::cast<clang::Expr>(s_it->first)};
          auto to_expr{clang::cast<clang::Expr>(s_it->second)};
          CopyProvenance(from_expr, to_expr);
        }
        change = true;
      }
    }
    return change;
  }

  void RunImpl() override { substitutions.clear(); }

 public:
  TransformVisitor(DecompilationContext &dec_ctx) : ASTPass(dec_ctx) {}

  virtual bool shouldTraversePostOrder() { return true; }

  bool VisitFunctionDecl(clang::FunctionDecl *fdecl) {
    // DLOG(INFO) << "VisitFunctionDecl";
    if (auto body = fdecl->getBody()) {
      auto iter = substitutions.find(body);
      if (iter != substitutions.end()) {
        fdecl->setBody(iter->second);
        changed = true;
      }
    }
    return !Stopped();
  }

  bool VisitStmt(clang::Stmt *stmt) {
    // DLOG(INFO) << "VisitStmt";
    changed |= ReplaceChildren(stmt, substitutions);
    return !Stopped();
  }
};

}  // namespace rellic
