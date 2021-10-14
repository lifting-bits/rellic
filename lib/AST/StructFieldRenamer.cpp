/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */
#include "rellic/AST/StructFieldRenamer.h"

#include <clang/AST/Decl.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/Support/Casting.h>

#include "rellic/AST/Compat/Stmt.h"

namespace rellic {

char StructFieldRenamer::ID = 0;

StructFieldRenamer::StructFieldRenamer(clang::ASTUnit &unit,
                                       IRTypeToDITypeMap &types_,
                                       IRToTypeDeclMap &decls_)
    : ModulePass(StructFieldRenamer::ID),
      ast(unit),
      ast_ctx(&unit.getASTContext()),
      types(types_),
      inv_decl(decls_) {}

bool StructFieldRenamer::VisitRecordDecl(clang::RecordDecl *decl) {
  auto *type = decls[decl];
  CHECK(type);

  auto *di = types[type];
  if (!di) {
    return true;
  }

  auto *ditype = llvm::cast<llvm::DICompositeType>(di);
  std::vector<clang::FieldDecl *> decl_fields;
  std::vector<llvm::DIDerivedType *> di_fields;

  for (auto field : decl->fields()) {
    decl_fields.push_back(field);
  }

  for (auto field : ditype->getElements()) {
    di_fields.push_back(llvm::cast<llvm::DIDerivedType>(field));
  }

  if (decl_fields.size() != di_fields.size()) {
    // Debug metadata is not compatible with bitcode, bail out
    // FIXME: Find a way to reconcile differences
    return true;
  }

  for (size_t i = 0; i < decl_fields.size(); i++) {
    auto *decl_field = decl_fields[i];
    auto *di_field = di_fields[i];

    auto old_name = decl_field->getName().str();
    auto name = di_field->getName().str();
    decl_field->setDeclName(ast.CreateIdentifier(name + "_" + old_name));
  }

  return true;
}

bool StructFieldRenamer::runOnModule(llvm::Module &module) {
  LOG(INFO) << "Renaming struct fields";
  Initialize();
  for (auto &pair : inv_decl) {
    decls[pair.second] = pair.first;
  }
  TraverseDecl(ast_ctx->getTranslationUnitDecl());
  return changed;
}

}  // namespace rellic
