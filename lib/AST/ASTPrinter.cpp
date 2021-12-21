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
#include <llvm/ADT/SmallString.h>
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

static void PrintFloatingLiteral(llvm::raw_ostream &os,
                                 clang::FloatingLiteral *lit,
                                 bool print_suffix) {
  llvm::SmallString<16> str;
  lit->getValue().toString(str);
  os << str;
  if (str.find_first_not_of("-0123456789") == llvm::StringRef::npos) {
    os << '.';  // Trailing dot in order to separate from ints.
  }

  if (!print_suffix) {
    return;
  }

  // Emit suffixes.  Float literals are always a builtin float type.
  switch (lit->getType()->castAs<clang::BuiltinType>()->getKind()) {
    case clang::BuiltinType::Half:
      break;  // FIXME: suffix?
    case clang::BuiltinType::Double:
      break;  // no suffix.
    case clang::BuiltinType::Float16:
      os << "F16";
      break;
    case clang::BuiltinType::Float:
      os << 'F';
      break;
    case clang::BuiltinType::LongDouble:
      os << 'L';
      break;
    case clang::BuiltinType::Float128:
      os << 'Q';
      break;
    default:
      LOG(FATAL) << "Unexpected type for float literal!";
  }
}

static void SpaceImpl(std::list<Token> &list) {
  list.push_back(Token::CreateSpace());
}

static void IndentImpl(std::list<Token> &list, unsigned level) {
  for (auto i{0U}; i < level; ++i) {
    list.push_back(Token::CreateIndent());
  }
}

static void NewlineImpl(std::list<Token> &list) {
  list.push_back(Token::CreateNewline());
}

}  // namespace

void DeclTokenizer::Space() { SpaceImpl(out); }
void DeclTokenizer::Indent() { IndentImpl(out, indent_level); }
void DeclTokenizer::Newline() { NewlineImpl(out); }

void DeclTokenizer::PrintPragmas(clang::Decl *decl) {}

void DeclTokenizer::PrintAttributes(clang::Decl *decl) {}

void DeclTokenizer::VisitVarDecl(clang::VarDecl *vdecl) {
  PrintPragmas(vdecl);

  clang::QualType vtype;
  if (vdecl->getTypeSourceInfo()) {
    vtype = vdecl->getTypeSourceInfo()->getType();
  }

  clang::StorageClass sc{vdecl->getStorageClass()};
  if (sc != clang::SC_None) {
    std::string scs_str{clang::VarDecl::getStorageClassSpecifierString(sc)};
    out.push_back(Token::CreateMisc(scs_str));
    Space();
  }

  switch (vdecl->getTSCSpec()) {
    case clang::TSCS_unspecified:
      break;

    case clang::TSCS___thread:
      out.push_back(Token::CreateMisc("__thread"));
      Space();
      break;

    case clang::TSCS__Thread_local:
      out.push_back(Token::CreateMisc("_Thread_local"));
      Space();
      break;

    case clang::TSCS_thread_local:
      out.push_back(Token::CreateMisc("thread_local"));
      Space();
      break;
  }

  if (vdecl->isModulePrivate()) {
    out.push_back(Token::CreateMisc("__module_private__"));
    Space();
  }

  std::string buf{""};
  llvm::raw_string_ostream ss(buf);
  vtype.print(ss, unit.getASTContext().getPrintingPolicy(), vdecl->getName(),
              indent_level);

  out.push_back(Token::CreateDecl(vdecl, ss.str()));

  if (auto init = vdecl->getInit()) {
    if (vdecl->getInitStyle() == clang::VarDecl::CInit) {
      Space();
      out.push_back(Token::CreateMisc("="));
      Space();
    } else {
      LOG(FATAL) << "Invalid initialization style.";
    }

    StmtTokenizer(out, unit).Visit(init);
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
      out.push_back(Token::CreateMisc(","));
      Space();
    }
    Visit(*begin);
  }
}

void DeclTokenizer::ProcessDeclGroup(
    llvm::SmallVectorImpl<clang::Decl *> &decls) {
  Indent();
  PrintGroup(decls.data(), decls.size());
  out.push_back(Token::CreateMisc(";"));
  Newline();
  decls.clear();
}

void DeclTokenizer::VisitDeclContext(clang::DeclContext *dctx, bool indent) {
  if (indent) {
    indent_level += 1;
  }

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

    Indent();
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
      out.push_back(Token::CreateMisc(terminator));
    }

    if ((clang::isa<clang::FunctionDecl>(*dit) &&
         clang::cast<clang::FunctionDecl>(*dit)
             ->doesThisDeclarationHaveABody())) {
      // StmtPrinter already added a newline after CompoundStmt.
    } else {
      Newline();
    }
  }

  if (!decls.empty()) {
    ProcessDeclGroup(decls);
  }

  if (indent) {
    indent_level -= 1;
  }
}

void DeclTokenizer::VisitFunctionDecl(clang::FunctionDecl *fdecl) {
  PrintPragmas(fdecl);

  switch (fdecl->getStorageClass()) {
    case clang::SC_None:
      break;

    case clang::SC_Extern:
      out.push_back(Token::CreateMisc("extern"));
      Space();
      break;

    case clang::SC_Static:
      out.push_back(Token::CreateMisc("static"));
      Space();
      break;

    case clang::SC_PrivateExtern:
      out.push_back(Token::CreateMisc("__private_extern__"));
      Space();
      break;

    case clang::SC_Auto:
    case clang::SC_Register:
      LOG(FATAL) << "Invalid function specifier";
  }

  if (fdecl->isInlineSpecified()) {
    out.push_back(Token::CreateMisc("inline"));
    Space();
  }

  if (fdecl->isModulePrivate()) {
    out.push_back(Token::CreateMisc("__module_private__"));
    Space();
  }

  std::list<Token> proto;
  proto.push_back(Token::CreateDecl(fdecl, fdecl->getName().str()));

  clang::QualType ftype{fdecl->getType()};
  while (const auto pt = clang::dyn_cast<clang::ParenType>(ftype)) {
    proto.push_front(Token::CreateMisc("("));
    proto.push_back(Token::CreateMisc(")"));
    ftype = pt->getInnerType();
  }

  if (const auto aft = ftype->getAs<clang::FunctionType>()) {
    const clang::FunctionProtoType *ft{nullptr};
    if (fdecl->hasWrittenPrototype()) {
      ft = clang::dyn_cast<clang::FunctionProtoType>(aft);
    }

    proto.push_back(Token::CreateMisc("("));

    if (ft) {
      DeclTokenizer param_tokenizer(proto, unit);
      for (auto i{0U}, e{fdecl->getNumParams()}; i != e; ++i) {
        if (i) {
          proto.push_back(Token::CreateMisc(","));
          SpaceImpl(proto);
        }
        param_tokenizer.VisitParmVarDecl(fdecl->getParamDecl(i));
      }

      if (ft->isVariadic()) {
        if (fdecl->getNumParams()) {
          proto.push_back(Token::CreateMisc(","));
          SpaceImpl(proto);
          proto.push_back(Token::CreateMisc("..."));
        }
      }
    } else if (fdecl->doesThisDeclarationHaveABody() &&
               !fdecl->hasPrototype()) {
      for (auto i{0U}, e{fdecl->getNumParams()}; i != e; ++i) {
        if (i) {
          proto.push_back(Token::CreateMisc(","));
          SpaceImpl(proto);
        }
        auto pdecl{fdecl->getParamDecl(i)};
        auto pname{pdecl->getNameAsString()};
        proto.push_back(Token::CreateDecl(pdecl, pname));
      }
    }

    proto.push_back(Token::CreateMisc(")"));

    if (ft) {
      if (ft->isConst()) {
        SpaceImpl(proto);
        proto.push_back(Token::CreateMisc("const"));
      }

      if (ft->isVolatile()) {
        SpaceImpl(proto);
        proto.push_back(Token::CreateMisc("volatile"));
      }

      if (ft->isRestrict()) {
        SpaceImpl(proto);
        proto.push_back(Token::CreateMisc("restrict"));
      }

      switch (ft->getRefQualifier()) {
        case clang::RQ_None:
          break;
        case clang::RQ_LValue:
          SpaceImpl(proto);
          proto.push_back(Token::CreateMisc("&"));
          break;
        case clang::RQ_RValue:
          SpaceImpl(proto);
          proto.push_back(Token::CreateMisc("&&"));
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
      out.push_back(Token::CreateType(aft->getReturnType(), ss.str()));
      // TODO(surovic): This should be removed once we have a type tokenizer
      Space();
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
    out.push_back(Token::CreateType(ftype, ss.str()));
  }

  PrintAttributes(fdecl);

  if (fdecl->doesThisDeclarationHaveABody()) {
    if (!fdecl->hasPrototype() && fdecl->getNumParams()) {
      // This is a K&R function definition, so we need to print the
      // parameters.
      Newline();
      for (auto i{0U}, e{fdecl->getNumParams()}; i != e; ++i) {
        Indent();
        VisitParmVarDecl(fdecl->getParamDecl(i));
        out.push_back(Token::CreateMisc(";"));
        Newline();
      }
    } else {
      Space();
    }

    if (fdecl->getBody()) {
      StmtTokenizer(out, unit).Visit(fdecl->getBody());
    }
  }
}

void DeclTokenizer::VisitTranslationUnitDecl(clang::TranslationUnitDecl *decl) {
  VisitDeclContext(decl, false);
}

void DeclTokenizer::VisitFieldDecl(clang::FieldDecl *fdecl) {
  // FIXME: add printing of pragma attributes if required.
  if (fdecl->isModulePrivate()) {
    out.push_back(Token::CreateMisc("__module_private__"));
    Space();
  }

  std::string buf{""};
  llvm::raw_string_ostream ss(buf);
  fdecl->getType().print(ss, unit.getASTContext().getPrintingPolicy(),
                         fdecl->getName(), indent_level);

  out.push_back(Token::CreateDecl(fdecl, ss.str()));

  if (fdecl->isBitField()) {
    Space();
    out.push_back(Token::CreateMisc(";"));
    Space();
    StmtTokenizer(out, unit).Visit(fdecl->getBitWidth());
  }

  PrintAttributes(fdecl);
}

void DeclTokenizer::VisitRecordDecl(clang::RecordDecl *rdecl) {
  if (rdecl->isModulePrivate()) {
    out.push_back(Token::CreateMisc("__module_private__"));
    Space();
  }

  out.push_back(Token::CreateMisc(rdecl->getKindName().str()));

  PrintAttributes(rdecl);

  if (rdecl->getIdentifier()) {
    Space();
    out.push_back(Token::CreateDecl(rdecl, rdecl->getNameAsString()));
  }

  if (rdecl->isCompleteDefinition()) {
    Space();
    out.push_back(Token::CreateMisc("{"));
    Newline();
    VisitDeclContext(rdecl);
    Indent();
    out.push_back(Token::CreateMisc("}"));
  }
}

void DeclTokenizer::VisitTypedefDecl(clang::TypedefDecl *decl) {
  auto &policy{unit.getASTContext().getPrintingPolicy()};
  if (!policy.SuppressSpecifiers) {
    out.push_back(Token::CreateMisc("typedef"));
    Space();

    if (decl->isModulePrivate()) {
      out.push_back(Token::CreateMisc("__module_private__ "));
    }
  }
  clang::QualType type = decl->getTypeSourceInfo()->getType();

  std::string buf{""};
  llvm::raw_string_ostream ss(buf);
  type.print(ss, policy, decl->getName(), indent_level);

  out.push_back(Token::CreateDecl(decl, ss.str()));
  PrintAttributes(decl);
}

void StmtTokenizer::Space() { SpaceImpl(out); }
void StmtTokenizer::Indent() { IndentImpl(out, indent_level); }
void StmtTokenizer::Newline() { NewlineImpl(out); }

void StmtTokenizer::PrintStmt(clang::Stmt *stmt) {
  indent_level += 1;
  if (stmt && clang::isa<clang::Expr>(stmt)) {
    Indent();
    Visit(stmt);
    out.push_back(Token::CreateMisc(";"));
    Newline();
  } else if (stmt) {
    Visit(stmt);
  } else {
    Indent();
    out.push_back(Token::CreateStmt(nullptr, "<<<NULL STATEMENT>>>"));
  }
  indent_level -= 1;
}

void StmtTokenizer::PrintRawInitStmt(clang::Stmt *stmt, unsigned prefix_width) {
  // FIXME: Cope better with odd prefix widths.
  indent_level += (prefix_width + 1) / 2;
  if (auto declstmt = clang::dyn_cast<clang::DeclStmt>(stmt))
    PrintRawDeclStmt(declstmt);
  else {
    PrintExpr(clang::cast<clang::Expr>(stmt));
  }

  out.push_back(Token::CreateMisc(";"));
  Space();
  indent_level -= (prefix_width + 1) / 2;
}

void StmtTokenizer::PrintExpr(clang::Expr *expr) {
  if (expr) {
    Visit(expr);
  } else {
    out.push_back(Token::CreateStmt(nullptr, "<null expr>"));
  }
}

void StmtTokenizer::PrintRawCompoundStmt(clang::CompoundStmt *stmt) {
  out.push_back(Token::CreateMisc("{"));
  Newline();
  for (auto child : stmt->body()) {
    PrintStmt(child);
  }
  Indent();
  out.push_back(Token::CreateMisc("}"));
}

void StmtTokenizer::VisitCompoundStmt(clang::CompoundStmt *stmt) {
  Indent();
  PrintRawCompoundStmt(stmt);
  Newline();
}

void StmtTokenizer::PrintRawDeclStmt(clang::DeclStmt *stmt) {
  llvm::SmallVector<clang::Decl *, 2> decls(stmt->decls());
  DeclTokenizer(out, unit).PrintGroup(decls.data(), decls.size());
}

void StmtTokenizer::VisitDeclStmt(clang::DeclStmt *stmt) {
  Indent();
  PrintRawDeclStmt(stmt);
  out.push_back(Token::CreateMisc(";"));
  Newline();
}

void StmtTokenizer::PrintRawIfStmt(clang::IfStmt *ifstmt) {
  out.push_back(Token::CreateStmt(ifstmt, "if"));
  Space();
  out.push_back(Token::CreateMisc("("));

  if (ifstmt->getInit()) {
    PrintRawInitStmt(ifstmt->getInit(), /*prefix_width="*/ 4U);
  }

  if (const auto ds = ifstmt->getConditionVariableDeclStmt()) {
    PrintRawDeclStmt(ds);
  } else {
    PrintExpr(ifstmt->getCond());
  }

  out.push_back(Token::CreateMisc(")"));

  if (auto cs = clang::dyn_cast<clang::CompoundStmt>(ifstmt->getThen())) {
    Space();
    PrintRawCompoundStmt(cs);
    if (ifstmt->getElse()) {
      Space();
    } else {
      Newline();
    }
  } else {
    Newline();
    PrintStmt(ifstmt->getThen());
    if (ifstmt->getElse()) {
      Indent();
    }
  }

  if (auto es = ifstmt->getElse()) {
    out.push_back(Token::CreateMisc("else"));

    if (auto cs = clang::dyn_cast<clang::CompoundStmt>(es)) {
      Space();
      PrintRawCompoundStmt(cs);
      Newline();
    } else if (auto elseif = clang::dyn_cast<clang::IfStmt>(es)) {
      Space();
      PrintRawIfStmt(elseif);
    } else {
      Newline();
      PrintStmt(ifstmt->getElse());
    }
  }
}

void StmtTokenizer::VisitIfStmt(clang::IfStmt *stmt) {
  Indent();
  PrintRawIfStmt(stmt);
}

void StmtTokenizer::VisitWhileStmt(clang::WhileStmt *stmt) {
  Indent();
  out.push_back(Token::CreateStmt(stmt, "while"));
  Space();
  out.push_back(Token::CreateMisc("("));
  if (const auto ds = stmt->getConditionVariableDeclStmt()) {
    PrintRawDeclStmt(ds);
  } else {
    PrintExpr(stmt->getCond());
  }
  out.push_back(Token::CreateMisc(")"));
  Newline();
  PrintStmt(stmt->getBody());
}

void StmtTokenizer::VisitDoStmt(clang::DoStmt *stmt) {
  Indent();
  out.push_back(Token::CreateStmt(stmt, "do"));
  Space();

  if (auto cs = clang::dyn_cast<clang::CompoundStmt>(stmt->getBody())) {
    PrintRawCompoundStmt(cs);
    Space();
  } else {
    Newline();
    PrintStmt(stmt->getBody());
    Indent();
  }

  out.push_back(Token::CreateStmt(stmt, "while"));
  Space();
  out.push_back(Token::CreateMisc("("));
  PrintExpr(stmt->getCond());
  out.push_back(Token::CreateMisc(")"));
  out.push_back(Token::CreateMisc(";"));
  Newline();
}

void StmtTokenizer::VisitBreakStmt(clang::BreakStmt *stmt) {
  Indent();
  out.push_back(Token::CreateStmt(stmt, "break"));
  out.push_back(Token::CreateMisc(";"));
  Newline();
}

void StmtTokenizer::VisitReturnStmt(clang::ReturnStmt *stmt) {
  Indent();
  out.push_back(Token::CreateStmt(stmt, "return"));
  if (stmt->getRetValue()) {
    Space();
    PrintExpr(stmt->getRetValue());
  }
  out.push_back(Token::CreateMisc(";"));
  Newline();
}

void StmtTokenizer::VisitIntegerLiteral(clang::IntegerLiteral *lit) {
  bool is_signed{lit->getType()->isSignedIntegerType()};
  llvm::SmallString<32U> str;
  lit->getValue().toString(str, 10U, is_signed);
  // Emit suffixes.  Integer literals are always a builtin integer type.
  switch (lit->getType()->castAs<clang::BuiltinType>()->getKind()) {
    case clang::BuiltinType::Char_S:
    case clang::BuiltinType::Char_U:
      str += "i8";
      break;

    case clang::BuiltinType::UChar:
      str += "Ui8";
      break;

    case clang::BuiltinType::Short:
      str += "i16";
      break;

    case clang::BuiltinType::UShort:
      str += "Ui16";
      break;

    case clang::BuiltinType::Int:
      break;  // no suffix.

    case clang::BuiltinType::UInt:
      str += 'U';
      break;

    case clang::BuiltinType::Long:
      str += 'L';
      break;

    case clang::BuiltinType::ULong:
      str += "UL";
      break;

    case clang::BuiltinType::LongLong:
      str += "LL";
      break;

    case clang::BuiltinType::ULongLong:
      str += "ULL";
      break;

    default:
      LOG(FATAL) << "Unexpected type for integer literal!";
      break;
  }

  out.push_back(Token::CreateStmt(lit, str.c_str()));
}

void StmtTokenizer::VisitFloatingLiteral(clang::FloatingLiteral *lit) {
  std::string buf;
  llvm::raw_string_ostream ss(buf);
  PrintFloatingLiteral(ss, lit, /*print_suffix=*/true);
  out.push_back(Token::CreateStmt(lit, ss.str()));
}

void StmtTokenizer::VisitStringLiteral(clang::StringLiteral *lit) {
  std::string buf;
  llvm::raw_string_ostream ss(buf);
  lit->outputString(ss);
  out.push_back(Token::CreateStmt(lit, ss.str()));
}

void StmtTokenizer::VisitInitListExpr(clang::InitListExpr *list) {
  if (list->getSyntacticForm()) {
    Visit(list->getSyntacticForm());
    return;
  }

  out.push_back(Token::CreateStmt(list, "{"));

  for (auto i{0U}, e{list->getNumInits()}; i != e; ++i) {
    if (i) {
      out.push_back(Token::CreateMisc(","));
      Space();
    }
    if (list->getInit(i)) {
      PrintExpr(list->getInit(i));
    } else {
      out.push_back(Token::CreateStmt(list, "{}"));
    }
  }

  out.push_back(Token::CreateStmt(list, "}"));
}

void StmtTokenizer::VisitCompoundLiteralExpr(clang::CompoundLiteralExpr *lit) {
  out.push_back(Token::CreateStmt(lit, "("));
  std::string buf;
  llvm::raw_string_ostream ss(buf);
  lit->getType().print(ss, unit.getASTContext().getPrintingPolicy());
  out.push_back(Token::CreateStmt(lit, ss.str()));
  out.push_back(Token::CreateStmt(lit, ")"));
  PrintExpr(lit->getInitializer());
}

void StmtTokenizer::VisitDeclRefExpr(clang::DeclRefExpr *ref) {
  auto name{ref->getNameInfo().getAsString()};
  out.push_back(Token::CreateStmt(ref, name));
}

void StmtTokenizer::VisitParenExpr(clang::ParenExpr *paren) {
  out.push_back(Token::CreateStmt(paren, "("));
  PrintExpr(paren->getSubExpr());
  out.push_back(Token::CreateStmt(paren, ")"));
}

void StmtTokenizer::VisitImplicitCastExpr(clang::ImplicitCastExpr *cast) {
  // No need to print anything, simply forward to the subexpression.
  PrintExpr(cast->getSubExpr());
}

void StmtTokenizer::VisitCStyleCastExpr(clang::CStyleCastExpr *cast) {
  std::string buf;
  llvm::raw_string_ostream ss(buf);
  ss << '(';
  cast->getTypeAsWritten().print(ss, unit.getASTContext().getPrintingPolicy());
  ss << ')';
  out.push_back(Token::CreateStmt(cast, ss.str()));
  Visit(cast->getSubExpr());
}

void StmtTokenizer::VisitArraySubscriptExpr(clang::ArraySubscriptExpr *sub) {
  PrintExpr(sub->getLHS());
  out.push_back(Token::CreateStmt(sub, "["));
  PrintExpr(sub->getRHS());
  out.push_back(Token::CreateStmt(sub, "]"));
}

void StmtTokenizer::VisitMemberExpr(clang::MemberExpr *member) {
  PrintExpr(member->getBase());

  auto pm{clang::dyn_cast<clang::MemberExpr>(member->getBase())};
  auto pd{pm ? clang::dyn_cast<clang::FieldDecl>(pm->getMemberDecl())
             : nullptr};

  if (!pd || !pd->isAnonymousStructOrUnion()) {
    out.push_back(Token::CreateMisc(member->isArrow() ? "->" : "."));
  }

  if (auto fd = clang::dyn_cast<clang::FieldDecl>(member->getMemberDecl())) {
    if (fd->isAnonymousStructOrUnion()) {
      return;
    }
  }

  auto name{member->getMemberNameInfo().getAsString()};
  out.push_back(Token::CreateStmt(member, name));
}

void StmtTokenizer::PrintCallArgs(clang::CallExpr *call) {
  for (auto i{0U}, e{call->getNumArgs()}; i != e; ++i) {
    if (i) {
      out.push_back(Token::CreateMisc(","));
      Space();
    }
    PrintExpr(call->getArg(i));
  }
}

void StmtTokenizer::VisitCallExpr(clang::CallExpr *call) {
  PrintExpr(call->getCallee());
  out.push_back(Token::CreateStmt(call, "("));
  PrintCallArgs(call);
  out.push_back(Token::CreateStmt(call, ")"));
}

void StmtTokenizer::VisitUnaryOperator(clang::UnaryOperator *unop) {
  auto opc_str{clang::UnaryOperator::getOpcodeStr(unop->getOpcode()).str()};
  if (!unop->isPostfix()) {
    out.push_back(Token::CreateStmt(unop, opc_str));

    // Print a space if this is an "identifier operator" like __real, or if
    // it might be concatenated incorrectly like '+'.
    switch (unop->getOpcode()) {
      default:
        break;
      case clang::UO_Real:
      case clang::UO_Imag:
      case clang::UO_Extension:
        Space();
        break;
      case clang::UO_Plus:
      case clang::UO_Minus:
        if (clang::isa<clang::UnaryOperator>(unop->getSubExpr())) {
          Space();
        }
        break;
    }
  }

  PrintExpr(unop->getSubExpr());

  if (unop->isPostfix()) {
    out.push_back(Token::CreateStmt(unop, opc_str));
  }
}

void StmtTokenizer::VisitBinaryOperator(clang::BinaryOperator *binop) {
  Visit(binop->getLHS());
  Space();
  out.push_back(Token::CreateStmt(binop, binop->getOpcodeStr().str()));
  Space();
  Visit(binop->getRHS());
}

void StmtTokenizer::VisitConditionalOperator(
    clang::ConditionalOperator *condop) {
  PrintExpr(condop->getCond());
  Space();
  out.push_back(Token::CreateStmt(condop, "?"));
  Space();
  PrintExpr(condop->getLHS());
  Space();
  out.push_back(Token::CreateStmt(condop, ":"));
  Space();
  PrintExpr(condop->getRHS());
}

}  // namespace rellic