/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/ASTContext.h>

#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/TransformVisitor.h"
#include "rellic/AST/Util.h"

namespace rellic {

/*
 * This pass simplifies conditions using Z3 by trying to remove terms that are
 * trivially true or false
 */
class MaterializeConds : public TransformVisitor<MaterializeConds> {
 private:
  IRToASTVisitor ast_gen;

 protected:
  void RunImpl() override;

 public:
  MaterializeConds(Provenance &provenance, clang::ASTUnit &unit);

  bool VisitIfStmt(clang::IfStmt *stmt);
  bool VisitWhileStmt(clang::WhileStmt *loop);
  bool VisitDoStmt(clang::DoStmt *loop);
};

}  // namespace rellic
