/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/ASTPrinter.h"

#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/Type.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/raw_ostream.h>

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

void DeclTokenizer::PrintPragmas(clang::Decl *decl) {}

void DeclTokenizer::PrintAttributes(clang::Decl *decl) {}

void DeclTokenizer::PrintDeclType(clang::QualType type,
                                  llvm::StringRef decl_name) {
  std::string buf{""};
  llvm::raw_string_ostream ss(buf);
  type.print(ss, unit.getASTContext().getPrintingPolicy(), decl_name,
             /* Indentation=*/0U);
  out.push_back({.str = ss.str()});
}

void DeclTokenizer::VisitVarDecl(clang::VarDecl *vdecl) {
  PrintPragmas(vdecl);

  clang::QualType vtype;
  if (vdecl->getTypeSourceInfo()) {
    vtype = vdecl->getTypeSourceInfo()->getType();
  } else {
    LOG(FATAL) << "There was ObjC stuff here. Not sure if it's needed.";
  }

  clang::StorageClass sc{vdecl->getStorageClass()};
  if (sc != clang::SC_None) {
    std::string scs_str{clang::VarDecl::getStorageClassSpecifierString(sc)};
    out.push_back({.str = scs_str + " "});
  }

  switch (vdecl->getTSCSpec()) {
    case clang::TSCS_unspecified:
      break;

    case clang::TSCS___thread:
      out.push_back({.str = "__thread "});
      break;

    case clang::TSCS__Thread_local:
      out.push_back({.str = "_Thread_local "});
      break;

    case clang::TSCS_thread_local:
      out.push_back({.str = "thread_local "});
      break;
  }

  if (vdecl->isModulePrivate()) {
    out.push_back({.str = "__module_private__ "});
  }

  PrintDeclType(vtype, vdecl->getName());
  out.back().node.decl = vdecl;

  if (clang::Expr *init = vdecl->getInit()) {
    if (vdecl->getInitStyle() == clang::VarDecl::CallInit &&
        !clang::isa<clang::ParenListExpr>(init)) {
      out.push_back({.str = "("});
    } else if (vdecl->getInitStyle() == clang::VarDecl::CInit) {
      out.push_back({.str = " = "});
    }

    StmtTokenizer(out, unit).Visit(init);

    if (vdecl->getInitStyle() == clang::VarDecl::CallInit &&
        !clang::isa<clang::ParenListExpr>(init)) {
      out.push_back({.str = ")"});
    }
  }

  PrintAttributes(vdecl);
}

void DeclTokenizer::VisitParmVarDecl(clang::ParmVarDecl *pdecl) {
  VisitVarDecl(pdecl);
}

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
  PrintPragmas(fdecl);

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

  std::list<Token> proto;
  proto.push_back({.node = {.decl = fdecl}, .str = fdecl->getName().str()});

  clang::QualType ftype{fdecl->getType()};
  while (const auto pt = clang::dyn_cast<clang::ParenType>(ftype)) {
    proto.push_front({.str = "("});
    proto.push_back({.str = ")"});
    ftype = pt->getInnerType();
  }

  if (const auto aft = ftype->getAs<clang::FunctionType>()) {
    const clang::FunctionProtoType *ft{nullptr};
    if (fdecl->hasWrittenPrototype()) {
      ft = clang::dyn_cast<clang::FunctionProtoType>(aft);
    }

    proto.push_back({.str = "("});

    if (ft) {
      DeclTokenizer param_tokenizer(proto, unit);
      for (auto i{0U}, e{fdecl->getNumParams()}; i != e; ++i) {
        if (i) {
          proto.push_back({.str = ", "});
        }
        param_tokenizer.VisitParmVarDecl(fdecl->getParamDecl(i));
      }

      if (ft->isVariadic()) {
        if (fdecl->getNumParams()) {
          proto.push_back({.str = ", "});
        }
        proto.push_back({.str = "..."});
      }
    } else if (fdecl->doesThisDeclarationHaveABody() &&
               !fdecl->hasPrototype()) {
      for (auto i{0U}, e{fdecl->getNumParams()}; i != e; ++i) {
        if (i) {
          proto.push_back({.str = ", "});
        }
        auto pdecl{fdecl->getParamDecl(i)};
        auto pname{pdecl->getNameAsString()};
        proto.push_back({.node = {.decl = pdecl}, .str = pname});
      }
    }

    proto.push_back({.str = ")"});

    if (ft) {
      if (ft->isConst()) {
        proto.push_back({.str = " const"});
      }

      if (ft->isVolatile()) {
        proto.push_back({.str = " volatile"});
      }

      if (ft->isRestrict()) {
        proto.push_back({.str = " restrict"});
      }

      switch (ft->getRefQualifier()) {
        case clang::RQ_None:
          break;
        case clang::RQ_LValue:
          proto.push_back({.str = " &"});
          break;
        case clang::RQ_RValue:
          proto.push_back({.str = " &&"});
          break;
      }
    }

    // TODO(surovic): Factor out this abomination (Look down)
    {
      std::string placeholder{""};
      // for (auto &tok : proto) {
      //   placeholder += tok.str;
      // }
      std::string buf{""};
      llvm::raw_string_ostream ss(buf);
      aft->getReturnType().print(ss, unit.getASTContext().getPrintingPolicy(),
                                 placeholder);
      out.push_back({{.type = aft->getReturnType()}, .str = ss.str()});
    }

    out.splice(out.end(), proto);

  } else {
    // TODO(surovic): Factor out this abomination (Look up)
    std::string placeholder{""};
    // for (auto &tok : proto) {
    //   placeholder += tok.str;
    // }
    std::string buf{""};
    llvm::raw_string_ostream ss(buf);
    ftype.print(ss, unit.getASTContext().getPrintingPolicy(), placeholder);
    out.push_back({{.type = ftype}, .str = ss.str()});
  }

  PrintAttributes(fdecl);

  if (fdecl->doesThisDeclarationHaveABody()) {
    if (!fdecl->hasPrototype() && fdecl->getNumParams()) {
      // This is a K&R function definition, so we need to print the
      // parameters.
      for (auto i{0U}, e{fdecl->getNumParams()}; i != e; ++i) {
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

void StmtTokenizer::PrintStmt(clang::Stmt *stmt) {
  if (stmt && clang::isa<clang::Expr>(stmt)) {
    out.push_back({.str = ";"});
    Visit(stmt);
  } else if (stmt) {
    Visit(stmt);
  } else {
    Token nulltok{.node = {.stmt = nullptr},
                  .str = "<<<NULL STATEMENT>>>" + nl};
    out.push_back(nulltok);
  }
}

void StmtTokenizer::PrintExpr(clang::Expr *expr) {
  if (expr) {
    Visit(expr);
  } else {
    out.push_back({.node = {.stmt = nullptr}, .str = "<null expr>"});
  }
}

void StmtTokenizer::VisitCompoundStmt(clang::CompoundStmt *stmt) {
  out.push_back({.str = "{"});
  for (auto child : stmt->body()) {
    PrintStmt(child);
  }
  out.push_back({.str = "}"});
}

void StmtTokenizer::VisitDeclStmt(clang::DeclStmt *stmt) {
  llvm::SmallVector<clang::Decl *, 2> decls(stmt->decls());
  DeclTokenizer(out, unit).PrintGroup(decls.data(), decls.size());
  out.push_back({.str = ";"});
}

void StmtTokenizer::VisitIfStmt(clang::IfStmt *ifstmt) {
  out.push_back({.node = {.stmt = ifstmt}, .str = "if"});
  out.push_back({.str = "("});

  if (ifstmt->getInit()) {
    Visit(ifstmt->getInit());
  }

  if (const auto ds = ifstmt->getConditionVariableDeclStmt()) {
    VisitDeclStmt(ds);
  } else {
    PrintExpr(ifstmt->getCond());
  }

  out.push_back({.str = ")"});

  if (auto cs = clang::dyn_cast<clang::CompoundStmt>(ifstmt->getThen())) {
    VisitCompoundStmt(cs);
    // OS << (If->getElse() ? " " : NL);
  } else {
    // OS << NL;
    PrintStmt(ifstmt->getThen());
  }

  if (auto es = ifstmt->getElse()) {
    out.push_back({.str = "else"});

    if (auto cs = clang::dyn_cast<clang::CompoundStmt>(es)) {
      VisitCompoundStmt(cs);
      // OS << NL;
    } else if (auto elseif = clang::dyn_cast<clang::IfStmt>(es)) {
      VisitIfStmt(elseif);
    } else {
      // OS << NL;
      PrintStmt(ifstmt->getElse());
    }
  }
}

void StmtTokenizer::VisitCStyleCastExpr(clang::CStyleCastExpr *cast) {
  std::string buf;
  llvm::raw_string_ostream ss(buf);
  ss << '(';
  cast->getTypeAsWritten().print(ss, unit.getASTContext().getPrintingPolicy());
  ss << ')';
  out.push_back({.node = {.stmt = cast}, .str = ss.str()});
  Visit(cast->getSubExpr());
}

void StmtTokenizer::VisitIntegerLiteral(clang::IntegerLiteral *ilit) {
  bool is_signed{ilit->getType()->isSignedIntegerType()};
  std::stringstream ss;
  ss << ilit->getValue().toString(10, is_signed);
  // Emit suffixes.  Integer literals are always a builtin integer type.
  switch (ilit->getType()->castAs<clang::BuiltinType>()->getKind()) {
    case clang::BuiltinType::Char_S:
    case clang::BuiltinType::Char_U:
      ss << "i8";
      break;

    case clang::BuiltinType::UChar:
      ss << "Ui8";
      break;

    case clang::BuiltinType::Short:
      ss << "i16";
      break;

    case clang::BuiltinType::UShort:
      ss << "Ui16";
      break;

    case clang::BuiltinType::Int:
      break;  // no suffix.

    case clang::BuiltinType::UInt:
      ss << 'U';
      break;

    case clang::BuiltinType::Long:
      ss << 'L';
      break;

    case clang::BuiltinType::ULong:
      ss << "UL";
      break;

    case clang::BuiltinType::LongLong:
      ss << "LL";
      break;

    case clang::BuiltinType::ULongLong:
      ss << "ULL";
      break;

    default:
      LOG(FATAL) << "Unexpected type for integer literal!";
      break;
  }

  out.push_back({.node = {.stmt = ilit}, .str = ss.str()});
}

}  // namespace rellic