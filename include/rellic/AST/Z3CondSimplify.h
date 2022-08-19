/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/ASTContext.h>

#include "rellic/AST/ASTPass.h"

namespace rellic {

/*
 * This pass simplifies conditions using Z3 by trying to remove terms that are
 * trivially true or false
 */
class Z3CondSimplify : public ASTPass {
 private:
 protected:
  void RunImpl() override;

 public:
  Z3CondSimplify(Provenance &provenance, clang::ASTUnit &unit);
};

}  // namespace rellic
