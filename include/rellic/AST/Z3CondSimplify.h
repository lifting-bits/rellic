/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/AST/Expr.h>
#include <clang/AST/StmtVisitor.h>

#include "rellic/AST/ASTPass.h"
#include "rellic/AST/Util.h"
#include "rellic/AST/Z3ConvVisitor.h"

namespace rellic {

/*
 * This pass simplifies conditions using Z3 by trying to remove terms that are
 * trivially true or false
 */
class Z3CondSimplify : public clang::StmtVisitor<Z3CondSimplify>,
                       public ASTPass {
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
        hash = GetHash(e);
      }
      return hash;
    }
  };

  struct KeyEqual {
    clang::ASTContext &ctx;
    bool operator()(clang::Expr *a, clang::Expr *b) const noexcept {
      return IsEquivalent(a, b);
    }
  };
  Hash hash_adaptor;
  KeyEqual ke_adaptor;

  std::unordered_map<clang::Expr *, bool, Hash, KeyEqual> proven_true;
  std::unordered_map<clang::Expr *, bool, Hash, KeyEqual> proven_false;

  bool IsProvenTrue(clang::Expr *e);
  bool IsProvenFalse(clang::Expr *e);

 protected:
  void RunImpl(clang::Stmt *stmt) override;

 public:
  Z3CondSimplify(StmtToIRMap &provenance, clang::ASTUnit &unit,
                 Substitutions &substitutions);

  z3::context &GetZ3Context() { return *z_ctx; }

  void SetZ3Tactic(z3::tactic t) { tactic = t; };

  void VisitBinaryOperator(clang::BinaryOperator *binop);
  void VisitUnaryOperator(clang::UnaryOperator *unop);
};

}  // namespace rellic
