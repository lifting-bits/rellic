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
  // FIXME: This should be on the Type class!
  auto base_type{type};
  while (!base_type->isSpecifierType()) {
    if (const auto pty = base_type->getAs<clang::PointerType>())
      base_type = pty->getPointeeType();
    else if (const auto bpy = base_type->getAs<clang::BlockPointerType>())
      base_type = bpy->getPointeeType();
    else if (const auto aty = clang::dyn_cast<clang::ArrayType>(base_type))
      base_type = aty->getElementType();
    else if (const auto fty = base_type->getAs<clang::FunctionType>())
      base_type = fty->getReturnType();
    else if (const auto vty = base_type->getAs<clang::VectorType>())
      base_type = vty->getElementType();
    else if (const auto pty = base_type->getAs<clang::ParenType>())
      base_type = pty->desugar();
    else
      // This must be a syntax error.
      break;
  }
  return base_type;
}

static clang::QualType GetDeclType(clang::Decl *decl) {
  if (auto tdd = clang::dyn_cast<clang::TypedefNameDecl>(decl)) {
    return tdd->getUnderlyingType();
  }

  if (auto vd = clang::dyn_cast<clang::ValueDecl>(decl)) {
    return vd->getType();
  }

  return clang::QualType();
}

}  // namespace

void DeclTokenizer::PrintGroup(clang::Decl **begin, unsigned num_decls) {
  if (num_decls == 1) {
    Visit(*begin);
    return;
  }

  clang::Decl **end{begin + num_decls};
  clang::TagDecl *td{clang::dyn_cast<clang::TagDecl>(*begin)};
  if (td) {
    ++begin;
  }

  bool is_first{true};
  for (; begin != end; ++begin) {
    if (is_first) {
      is_first = false;
    } else {
      out.push_back({.node = {}, .str = ", "});
    }
    Visit(*begin);
  }
}

void DeclTokenizer::ProcessDeclGroup(
    llvm::SmallVectorImpl<clang::Decl *> &decls) {
  PrintGroup(decls.data(), decls.size());
  out.push_back({.node = {}, .str = ";"});
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
      out.push_back({.node = {}, .str = terminator});
    }
  }
}

void DeclTokenizer::VisitFunctionDecl(clang::FunctionDecl *fdecl) {
  switch (fdecl->getStorageClass()) {
    case clang::SC_None:
      break;

    case clang::SC_Extern:
      out.push_back({.str = "extern "});
      break;

    case clang::SC_Static:
      out.push_back({.str = "static "});
      break;

    case clang::SC_PrivateExtern:
      out.push_back({.str = "__private_extern__ "});
      break;

    case clang::SC_Auto:
    case clang::SC_Register:
      LOG(FATAL) << "Invalid function specifier";
  }

  if (fdecl->isInlineSpecified()) {
    out.push_back({.str = "inline "});
  }

  if (fdecl->isModulePrivate()) {
    out.push_back({.str = "__module_private__ "});
  }

  std::string Proto{fdecl->getName().str()};
  out.push_back({.node = {.decl = fdecl}, .str = fdecl->getName().str()});

  clang::QualType Ty{fdecl->getType()};
  while (const auto PT = clang::dyn_cast<clang::ParenType>(Ty)) {
    Proto = '(' + Proto + ')';
    Ty = PT->getInnerType();
  }

  if (const auto AFT = Ty->getAs<clang::FunctionType>()) {
    const clang::FunctionProtoType *FT{nullptr};
    if (fdecl->hasWrittenPrototype()) {
      FT = clang::dyn_cast<clang::FunctionProtoType>(AFT);
    }

    Proto += "(";

    if (FT) {
      llvm::raw_string_ostream POut(Proto);
      for (auto i{0U}, e = fdecl->getNumParams(); i != e; ++i) {
        if (i) {
          POut << ", ";
        }
        VisitParmVarDecl(fdecl->getParamDecl(i));
      }

      if (FT->isVariadic()) {
        if (fdecl->getNumParams()) {
          POut << ", ";
        }
        POut << "...";
      }
    } else if (fdecl->doesThisDeclarationHaveABody() &&
               !fdecl->hasPrototype()) {
      for (auto i{0U}, e = fdecl->getNumParams(); i != e; ++i) {
        if (i) {
          Proto += ", ";
        }
        Proto += fdecl->getParamDecl(i)->getNameAsString();
      }
    }

    Proto += ")";

    if (FT) {
      if (FT->isConst()) {
        Proto += " const";
      }
      if (FT->isVolatile()) {
        Proto += " volatile";
      }
      if (FT->isRestrict()) {
        Proto += " restrict";
      }

      switch (FT->getRefQualifier()) {
        case clang::RQ_None:
          break;
        case clang::RQ_LValue:
          Proto += " &";
          break;
        case clang::RQ_RValue:
          Proto += " &&";
          break;
      }
    }
    Out << Proto;
  } else {
    Ty.print(Out, Policy, Proto);
  }

  if (fdecl->doesThisDeclarationHaveABody()) {
    if (!fdecl->hasPrototype() && fdecl->getNumParams()) {
      // This is a K&R function definition, so we need to print the
      // parameters.
      for (auto i{0U}, e = fdecl->getNumParams(); i != e; ++i) {
        VisitParmVarDecl(fdecl->getParamDecl(i));
        out.push_back({{}, ";"});
      }
    }

    if (fdecl->getBody()) {
      StmtTokenizer(out, unit).Visit(fdecl->getBody());
    }
  }
}

void DeclTokenizer::VisitTranslationUnitDecl(clang::TranslationUnitDecl *decl) {
  VisitDeclContext(decl);
}

}  // namespace rellic