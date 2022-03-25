/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/ASTContext.h>

#include "rellic/AST/TransformVisitor.h"
#include "rellic/AST/Util.h"
#include "rellic/AST/Z3ConvVisitor.h"

namespace rellic {

/*
 * This pass simplifies conditions using Z3 by trying to remove terms that are
 * trivially true or false
 */
class Z3CondSimplify : public TransformVisitor<Z3CondSimplify> {
 private:
  std::unique_ptr<z3::context> z_ctx;
  std::unique_ptr<rellic::Z3ConvVisitor> z_gen;

  z3::tactic tactic;

  bool Prove(z3::expr e);
  z3::expr ToZ3(clang::Expr *e);

  std::unordered_map<clang::Expr *, unsigned> hashes;

  struct Hash {
    clang::ASTContext &ctx;
    std::unordered_map<clang::Expr *, unsigned> &hashes;
    std::size_t operator()(clang::Expr *e) const noexcept {
      auto &hash{hashes[e]};
      if (!hash) {
        hash = GetHash(ctx, e);
      }
      return hash;
    }
  };

  struct KeyEqual {
    clang::ASTContext &ctx;
    bool operator()(clang::Expr *a, clang::Expr *b) const noexcept {
      return IsEquivalent(ctx, a, b);
    }
  };
  Hash hash_adaptor;
  KeyEqual ke_adaptor;

  std::unordered_map<clang::Expr *, bool, Hash, KeyEqual> proven_true;
  std::unordered_map<clang::Expr *, bool, Hash, KeyEqual> proven_false;

  bool IsProvenTrue(clang::Expr *e);
  bool IsProvenFalse(clang::Expr *e);

  clang::Expr *Simplify(clang::Expr *e);

 protected:
  void RunImpl() override;

 public:
  Z3CondSimplify(StmtToIRMap &provenance, ExprToUseMap &use_provenance,
                 clang::ASTUnit &unit);

  z3::context &GetZ3Context() { return *z_ctx; }

  void SetZ3Tactic(z3::tactic t) { tactic = t; };

  bool VisitIfStmt(clang::IfStmt *stmt);
  bool VisitWhileStmt(clang::WhileStmt *loop);
  bool VisitDoStmt(clang::DoStmt *loop);
};

}  // namespace rellic
