/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/ASTPrinter.h"

#include <clang/AST/Type.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/ADT/SmallVector.h>

#include <sstream>

namespace rellic {

namespace {

static clang::QualType GetBaseType(clang::QualType type) {
  return clang::QualType();
}

static clang::QualType GetDeclType(clang::Decl *decl) {
  return clang::QualType();
}

}  // namespace

void DeclTokenizer::PrintGroup(clang::Decl **begin, unsigned num_decls) {
  if (num_decls == 1) {
    // (*Begin)->print(Out, Policy, Indentation);
    Visit(*begin);
    return;
  }

  clang::Decl **end{begin + num_decls};
  clang::TagDecl *td{clang::dyn_cast<clang::TagDecl>(*begin)};
  if (td) {
    ++begin;
  }

  clang::PrintingPolicy SubPolicy(Policy);

  bool is_first{true};
  for (; begin != end; ++begin) {
    if (is_first) {
      if (td) SubPolicy.IncludeTagDefinition = true;
      SubPolicy.SuppressSpecifiers = false;
      is_first = false;
    } else {
      if (!is_first) Out << ", ";
      SubPolicy.IncludeTagDefinition = false;
      SubPolicy.SuppressSpecifiers = true;
    }

    (*begin)->print(Out, SubPolicy, Indentation);
  }
}

void DeclTokenizer::ProcessDeclGroup(
    llvm::SmallVectorImpl<clang::Decl *> &decls) {
  clang::Decl::printGroup(decls.data(), decls.size(), Out, Policy, Indentation);
  out.back().str += ";";
  decls.clear();
}

void DeclTokenizer::VisitDeclContext(clang::DeclContext *dctx) {
  llvm::SmallVector<clang::Decl *, 2U> decls;
  for (auto dit = dctx->decls_begin(), dend = dctx->decls_end(); dit != dend;
       ++dit) {
    // Skip over implicit declarations in pretty-printing mode.
    if (dit->isImplicit()) {
      continue;
    }

    // The next bits of code handle stuff like "struct {int x;} a,b"; we're
    // forced to merge the declarations because there's no other way to
    // refer to the struct in question.  When that struct is named instead, we
    // also need to merge to avoid splitting off a stand-alone struct
    // declaration that produces the warning ext_no_declarators in some
    // contexts.
    //
    // This limited merging is safe without a bunch of other checks because it
    // only merges declarations directly referring to the tag, not typedefs.
    //
    // Check whether the current declaration should be grouped with a previous
    // non-free-standing tag declaration.
    auto cur_decl_type{GetDeclType(*dit)};
    if (!decls.empty() && !cur_decl_type.isNull()) {
      clang::QualType base_type{GetBaseType(cur_decl_type)};
      if (!base_type.isNull() && clang::isa<clang::ElaboratedType>(base_type) &&
          clang::cast<clang::ElaboratedType>(base_type)->getOwnedTagDecl() ==
              decls[0]) {
        decls.push_back(*dit);
        continue;
      }
    }

    // If we have a merged group waiting to be handled, handle it now.
    if (!decls.empty()) {
      ProcessDeclGroup(decls);
    }

    // If the current declaration is not a free standing declaration, save it
    // so we can merge it with the subsequent declaration(s) using it.
    if (clang::isa<clang::TagDecl>(*dit) &&
        !clang::cast<clang::TagDecl>(*dit)->isFreeStanding()) {
      decls.push_back(*dit);
      continue;
    }

    Visit(*dit);

    auto dtok{out.back()};

    const char *terminator{nullptr};
    if (auto fdecl = clang::dyn_cast<clang::FunctionDecl>(*dit)) {
      if (fdecl->isThisDeclarationADefinition())
        terminator = nullptr;
      else
        terminator = ";";
    } else if (clang::isa<clang::EnumConstantDecl>(*dit)) {
      auto next{dit};
      ++next;
      if (next != dend) {
        terminator = ",";
      }
    } else {
      terminator = ";";
    }

    if (terminator) {
      dtok.str += terminator;
    }
  }
}

void DeclTokenizer::VisitTranslationUnitDecl(clang::TranslationUnitDecl *decl) {
  VisitDeclContext(decl);
}

}  // namespace rellic