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

void DeclTokenizer::Indent() {
  for (auto i{0U}; i < indent_level; ++i) {
    out.push_back(Token::CreateIndent());
  }
}

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
    out.push_back(Token::CreateSpace());
  }

  switch (vdecl->getTSCSpec()) {
    case clang::TSCS_unspecified:
      break;

    case clang::TSCS___thread:
      out.push_back(Token::CreateMisc("__thread"));
      out.push_back(Token::CreateSpace());
      break;

    case clang::TSCS__Thread_local:
      out.push_back(Token::CreateMisc("_Thread_local"));
      out.push_back(Token::CreateSpace());
      break;

    case clang::TSCS_thread_local:
      out.push_back(Token::CreateMisc("thread_local"));
      out.push_back(Token::CreateSpace());
      break;
  }

  if (vdecl->isModulePrivate()) {
    out.push_back(Token::CreateMisc("__module_private__"));
    out.push_back(Token::CreateSpace());
  }

  std::string buf{""};
  llvm::raw_string_ostream ss(buf);
  vtype.print(ss, unit.getASTContext().getPrintingPolicy(), vdecl->getName(),
              indent_level);

  out.push_back(Token::CreateDecl(vdecl, ss.str()));

  if (auto init = vdecl->getInit()) {
    if (vdecl->getInitStyle() == clang::VarDecl::CInit) {
      out.push_back(Token::CreateSpace());
      out.push_back(Token::CreateMisc("="));
      out.push_back(Token::CreateSpace());
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
      out.push_back(Token::CreateSpace());
    }
    Visit(*begin);
  }
}

void DeclTokenizer::ProcessDeclGroup(
    llvm::SmallVectorImpl<clang::Decl *> &decls) {
  Indent();
  PrintGroup(decls.data(), decls.size());
  out.push_back(Token::CreateMisc(";"));
  out.push_back(Token::CreateNewline());
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
      out.push_back(Token::CreateNewline());
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
      out.push_back(Token::CreateSpace());
      break;

    case clang::SC_Static:
      out.push_back(Token::CreateMisc("static"));
      out.push_back(Token::CreateSpace());
      break;

    case clang::SC_PrivateExtern:
      out.push_back(Token::CreateMisc("__private_extern__"));
      out.push_back(Token::CreateSpace());
      break;

    case clang::SC_Auto:
    case clang::SC_Register:
      LOG(FATAL) << "Invalid function specifier";
  }

  if (fdecl->isInlineSpecified()) {
    out.push_back(Token::CreateMisc("inline"));
    out.push_back(Token::CreateSpace());
  }

  if (fdecl->isModulePrivate()) {
    out.push_back(Token::CreateMisc("__module_private__"));
    out.push_back(Token::CreateSpace());
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
          proto.push_back(Token::CreateSpace());
        }
        param_tokenizer.VisitParmVarDecl(fdecl->getParamDecl(i));
      }

      if (ft->isVariadic()) {
        if (fdecl->getNumParams()) {
          proto.push_back(Token::CreateMisc(","));
          proto.push_back(Token::CreateSpace());
        }
        proto.push_back(Token::CreateMisc("..."));
      }
    } else if (fdecl->doesThisDeclarationHaveABody() &&
               !fdecl->hasPrototype()) {
      for (auto i{0U}, e{fdecl->getNumParams()}; i != e; ++i) {
        if (i) {
          proto.push_back(Token::CreateMisc(","));
          proto.push_back(Token::CreateSpace());
        }
        auto pdecl{fdecl->getParamDecl(i)};
        auto pname{pdecl->getNameAsString()};
        proto.push_back(Token::CreateDecl(pdecl, pname));
      }
    }

    proto.push_back(Token::CreateMisc(")"));

    if (ft) {
      if (ft->isConst()) {
        proto.push_back(Token::CreateSpace());
        proto.push_back(Token::CreateMisc("const"));
      }

      if (ft->isVolatile()) {
        proto.push_back(Token::CreateSpace());
        proto.push_back(Token::CreateMisc("volatile"));
      }

      if (ft->isRestrict()) {
        proto.push_back(Token::CreateSpace());
        proto.push_back(Token::CreateMisc("restrict"));
      }

      switch (ft->getRefQualifier()) {
        case clang::RQ_None:
          break;
        case clang::RQ_LValue:
          proto.push_back(Token::CreateSpace());
          proto.push_back(Token::CreateMisc("&"));
          break;
        case clang::RQ_RValue:
          proto.push_back(Token::CreateSpace());
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
      out.push_back(Token::CreateSpace());
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
      out.push_back(Token::CreateNewline());
      for (auto i{0U}, e{fdecl->getNumParams()}; i != e; ++i) {
        Indent();
        VisitParmVarDecl(fdecl->getParamDecl(i));
        out.push_back(Token::CreateMisc(";"));
        out.push_back(Token::CreateNewline());
      }
    } else {
      out.push_back(Token::CreateSpace());
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
    out.push_back(Token::CreateSpace());
  }

  std::string buf{""};
  llvm::raw_string_ostream ss(buf);
  fdecl->getType().print(ss, unit.getASTContext().getPrintingPolicy(),
                         fdecl->getName(), indent_level);

  out.push_back(Token::CreateDecl(fdecl, ss.str()));

  if (fdecl->isBitField()) {
    out.push_back(Token::CreateSpace());
    out.push_back(Token::CreateMisc(";"));
    out.push_back(Token::CreateSpace());
    StmtTokenizer(out, unit).Visit(fdecl->getBitWidth());
  }

  PrintAttributes(fdecl);
}

void DeclTokenizer::VisitRecordDecl(clang::RecordDecl *rdecl) {
  if (rdecl->isModulePrivate()) {
    out.push_back(Token::CreateMisc("__module_private__"));
    out.push_back(Token::CreateSpace());
  }

  out.push_back(Token::CreateMisc(rdecl->getKindName().str()));

  PrintAttributes(rdecl);

  if (rdecl->getIdentifier()) {
    out.push_back(Token::CreateSpace());
    out.push_back(Token::CreateDecl(rdecl, rdecl->getNameAsString()));
  }

  if (rdecl->isCompleteDefinition()) {
    out.push_back(Token::CreateSpace());
    out.push_back(Token::CreateMisc("{"));
    out.push_back(Token::CreateNewline());
    VisitDeclContext(rdecl);
    Indent();
    out.push_back(Token::CreateMisc("}"));
  }
}

void StmtTokenizer::Indent() {
  for (auto i{0U}; i < indent_level; ++i) {
    out.push_back(Token::CreateIndent());
  }
}

void StmtTokenizer::PrintStmt(clang::Stmt *stmt) {
  indent_level += 1;
  if (stmt && clang::isa<clang::Expr>(stmt)) {
    Indent();
    Visit(stmt);
    out.push_back(Token::CreateMisc(";"));
    out.push_back(Token::CreateNewline());
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
  out.push_back(Token::CreateSpace());
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
  out.push_back(Token::CreateNewline());
  for (auto child : stmt->body()) {
    PrintStmt(child);
  }
  Indent();
  out.push_back(Token::CreateMisc("}"));
}

void StmtTokenizer::VisitCompoundStmt(clang::CompoundStmt *stmt) {
  Indent();
  PrintRawCompoundStmt(stmt);
  out.push_back(Token::CreateNewline());
}

void StmtTokenizer::PrintRawDeclStmt(clang::DeclStmt *stmt) {
  llvm::SmallVector<clang::Decl *, 2> decls(stmt->decls());
  DeclTokenizer(out, unit).PrintGroup(decls.data(), decls.size());
}

void StmtTokenizer::VisitDeclStmt(clang::DeclStmt *stmt) {
  Indent();
  PrintRawDeclStmt(stmt);
  out.push_back(Token::CreateMisc(";"));
  out.push_back(Token::CreateNewline());
}

void StmtTokenizer::PrintRawIfStmt(clang::IfStmt *ifstmt) {
  out.push_back(Token::CreateStmt(ifstmt, "if"));
  out.push_back(Token::CreateSpace());
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
    out.push_back(Token::CreateSpace());
    PrintRawCompoundStmt(cs);
    out.push_back(ifstmt->getElse() ? Token::CreateSpace()
                                    : Token::CreateNewline());
  } else {
    out.push_back(Token::CreateNewline());
    PrintStmt(ifstmt->getThen());
    if (ifstmt->getElse()) {
      Indent();
    }
  }

  if (auto es = ifstmt->getElse()) {
    out.push_back(Token::CreateMisc("else"));

    if (auto cs = clang::dyn_cast<clang::CompoundStmt>(es)) {
      out.push_back(Token::CreateSpace());
      PrintRawCompoundStmt(cs);
      out.push_back(Token::CreateNewline());
    } else if (auto elseif = clang::dyn_cast<clang::IfStmt>(es)) {
      out.push_back(Token::CreateSpace());
      PrintRawIfStmt(elseif);
    } else {
      out.push_back(Token::CreateNewline());
      PrintStmt(ifstmt->getElse());
    }
  }
}

void StmtTokenizer::VisitIfStmt(clang::IfStmt *ifstmt) {
  Indent();
  PrintRawIfStmt(ifstmt);
}

void StmtTokenizer::VisitReturnStmt(clang::ReturnStmt *stmt) {
  Indent();
  out.push_back(Token::CreateStmt(stmt, "return"));
  if (stmt->getRetValue()) {
    out.push_back(Token::CreateSpace());
    PrintExpr(stmt->getRetValue());
  }
  out.push_back(Token::CreateMisc(";"));
  out.push_back(Token::CreateNewline());
}

void StmtTokenizer::VisitIntegerLiteral(clang::IntegerLiteral *ilit) {
  bool is_signed{ilit->getType()->isSignedIntegerType()};
  std::stringstream ss;
  ss << ilit->getValue().toString(10U, is_signed);
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

  out.push_back(Token::CreateStmt(ilit, ss.str()));
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

void StmtTokenizer::PrintCallArgs(clang::CallExpr *call) {
  for (auto i{0U}, e{call->getNumArgs()}; i != e; ++i) {
    if (i) {
      out.push_back(Token::CreateMisc(","));
      out.push_back(Token::CreateSpace());
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
        out.push_back(Token::CreateSpace());
        break;
      case clang::UO_Plus:
      case clang::UO_Minus:
        if (clang::isa<clang::UnaryOperator>(unop->getSubExpr())) {
          out.push_back(Token::CreateSpace());
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
  out.push_back(Token::CreateSpace());
  out.push_back(Token::CreateStmt(binop, binop->getOpcodeStr().str()));
  out.push_back(Token::CreateSpace());
  Visit(binop->getRHS());
}

void StmtTokenizer::VisitConditionalOperator(
    clang::ConditionalOperator *condop) {
  PrintExpr(condop->getCond());
  out.push_back(Token::CreateSpace());
  out.push_back(Token::CreateStmt(condop, "?"));
  out.push_back(Token::CreateSpace());
  PrintExpr(condop->getLHS());
  out.push_back(Token::CreateSpace());
  out.push_back(Token::CreateStmt(condop, ":"));
  out.push_back(Token::CreateSpace());
  PrintExpr(condop->getRHS());
}

}  // namespace rellic