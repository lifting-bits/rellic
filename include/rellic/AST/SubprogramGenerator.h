/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/Decl.h>
#include <llvm/IR/DebugInfoMetadata.h>

#include <unordered_map>
#include <vector>

#include "rellic/AST/ASTBuilder.h"
#include "rellic/AST/StructGenerator.h"

namespace rellic {

class SubprogramGenerator {
  clang::ASTContext& ast_ctx;
  StructGenerator& struct_gen;
  rellic::ASTBuilder ast;

 public:
  SubprogramGenerator(clang::ASTUnit& ast_unit, StructGenerator& struct_gen);
  clang::FunctionDecl* VisitSubprogram(llvm::DISubprogram* subp);
};
}  // namespace rellic