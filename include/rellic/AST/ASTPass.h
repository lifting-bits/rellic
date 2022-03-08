/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once
#include <clang/AST/ASTContext.h>
#include <clang/Frontend/ASTUnit.h>
#include <rellic/AST/ASTBuilder.h>
#include <rellic/AST/Util.h>

#include <atomic>
#include <memory>

namespace rellic {

struct Substitution {
  clang::Stmt* before;
  clang::Stmt* after;
};

using Substitutions = std::vector<Substitution>;

class ASTPass {
  std::atomic_bool stop{false};

 protected:
  StmtToIRMap& provenance;
  clang::ASTUnit& ast_unit;
  clang::ASTContext& ast_ctx;
  Substitutions& substitutions;
  ASTBuilder ast;

  virtual void RunImpl(clang::Stmt* stmt) = 0;
  virtual void StopImpl() {}

 public:
  ASTPass(StmtToIRMap& provenance, clang::ASTUnit& ast_unit,
          Substitutions& substitutions)
      : provenance(provenance),
        ast_unit(ast_unit),
        ast_ctx(ast_unit.getASTContext()),
        ast(ast_unit),
        substitutions(substitutions) {}
  virtual ~ASTPass() = default;
  void Stop() {
    stop = true;
    StopImpl();
  }

  void Run(clang::Stmt* stmt) {
    stop = false;
    RunImpl(stmt);
  }

  bool Stopped() { return stop; }
};
}  // namespace rellic