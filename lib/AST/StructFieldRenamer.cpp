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

#include <unordered_set>

#include "rellic/AST/Compat/Stmt.h"

namespace rellic {

char StructFieldRenamer::ID = 0;

StructFieldRenamer::StructFieldRenamer(clang::ASTUnit &unit,
                                       IRTypeToDITypeMap &types,
                                       IRToTypeDeclMap &decls)
    : ModulePass(StructFieldRenamer::ID),
      ast(unit),
      ast_ctx(&unit.getASTContext()),
      types(types),
      inv_decl(decls) {}

bool StructFieldRenamer::VisitRecordDecl(clang::RecordDecl *decl) {
  auto type{decls[decl]};
  CHECK(type) << "Type information not present for declaration";

  auto di{types[type]};
  if (!di) {
    return true;
  }

  auto ditype = llvm::cast<llvm::DICompositeType>(di);
  std::vector<clang::FieldDecl *> decl_fields;
  std::vector<llvm::DIDerivedType *> di_fields;

  for (auto field : decl->fields()) {
    decl_fields.push_back(field);
  }

  for (auto field : ditype->getElements()) {
    di_fields.push_back(llvm::cast<llvm::DIDerivedType>(field));
  }

  std::unordered_set<std::string> seen_names;

  for (auto i{0U}; i < decl_fields.size(); ++i) {
    auto decl_field{decl_fields[i]};
    auto di_field{di_fields[i]};

    // FIXME(frabert): Is a clash between field names actually possible?
    // Can this mechanism actually be left out?
    auto name{di_field->getName().str()};
    if (di_field->getTag() == llvm::dwarf::DW_TAG_inheritance) {
      name = di_field->getBaseType()->getName().str() + "_base";
    }
    if (seen_names.find(name) == seen_names.end()) {
      seen_names.insert(name);
      decl_field->setDeclName(ast.CreateIdentifier(name));
    } else {
      auto old_name{decl_field->getName().str()};
      decl_field->setDeclName(ast.CreateIdentifier(name + "_" + old_name));
    }
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
