/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */
#include <clang/AST/Decl.h>

#include <string>
#define GOOGLE_STRIP_LOG 1

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/SubprogramGenerator.h"

namespace rellic {
SubprogramGenerator::SubprogramGenerator(clang::ASTUnit& ast_unit,
                                         StructGenerator& struct_gen)
    : ast_ctx(ast_unit.getASTContext()),
      struct_gen(struct_gen),
      ast(ast_unit) {}

clang::FunctionDecl* SubprogramGenerator::VisitSubprogram(
    llvm::DISubprogram* subp) {
  std::string name{};
  auto linkageName{subp->getLinkageName().str()};
  if (linkageName == "") {
    name = subp->getName();
  } else {
    name = linkageName;
  }
  CHECK_NE(name, "");

  auto type{struct_gen.VisitType(subp->getType())};
  auto tudecl{ast_ctx.getTranslationUnitDecl()};
  auto fdecl{ast.CreateFunctionDecl(tudecl, type, name)};
  auto type_arr{subp->getType()->getTypeArray()};
  std::vector<clang::ParmVarDecl*> params{};
  for (auto i{1U}; i < type_arr.size(); i++) {
    if (!type_arr[i]) {
      break;
    }
    // TODO(frabert): Extract names from bitcode if available
    auto parmname{"arg" + std::to_string(i)};
    params.push_back(ast.CreateParamDecl(
        fdecl, struct_gen.VisitType(type_arr[i]), parmname));
  }
  fdecl->setParams(params);
  tudecl->addDecl(fdecl);
  return fdecl;
}

}  // namespace rellic