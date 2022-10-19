//===--- DeclPrinter.cpp - Printing implementation for Decl ASTs ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Decl::print method, which pretty prints the
// AST back out to C/Objective-C/C++/Objective-C++ code.
//
//===----------------------------------------------------------------------===//
#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclObjC.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/DeclVisitor.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/Basic/Module.h>
#include <llvm/Support/raw_ostream.h>

#include <cstdint>

#include "Printer.h"
using namespace clang;

namespace {
class DeclPrinter : public DeclVisitor<DeclPrinter> {
  raw_ostream &Out;
  PrintingPolicy Policy;
  const ASTContext &Context;
  unsigned Indentation;
  bool PrintInstantiation;

  raw_ostream &Indent() { return Indent(Indentation); }
  raw_ostream &Indent(unsigned Indentation);
  void ProcessDeclGroup(SmallVectorImpl<Decl *> &Decls);

  void Print(AccessSpecifier AS);
  void PrintConstructorInitializers(CXXConstructorDecl *CDecl,
                                    std::string &Proto);

  /// Print an Objective-C method type in parentheses.
  ///
  /// \param Quals The Objective-C declaration qualifiers.
  /// \param T The type to print.
  void PrintObjCMethodType(ASTContext &Ctx, Decl::ObjCDeclQualifier Quals,
                           QualType T);

  void PrintObjCTypeParams(ObjCTypeParamList *Params);

 public:
  DeclPrinter(raw_ostream &Out, const PrintingPolicy &Policy,
              const ASTContext &Context, unsigned Indentation = 0,
              bool PrintInstantiation = false)
      : Out(Out),
        Policy(Policy),
        Context(Context),
        Indentation(Indentation),
        PrintInstantiation(PrintInstantiation) {}

  void VisitDeclContext(DeclContext *DC, bool Indent = true);

  void VisitTranslationUnitDecl(TranslationUnitDecl *D);
  void VisitTypedefDecl(TypedefDecl *D);
  void VisitTypeAliasDecl(TypeAliasDecl *D);
  void VisitEnumDecl(EnumDecl *D);
  void VisitRecordDecl(RecordDecl *D);
  void VisitEnumConstantDecl(EnumConstantDecl *D);
  void VisitEmptyDecl(EmptyDecl *D);
  void VisitFunctionDecl(FunctionDecl *D);
  void VisitFriendDecl(FriendDecl *D);
  void VisitFieldDecl(FieldDecl *D);
  void VisitVarDecl(VarDecl *D);
  void VisitLabelDecl(LabelDecl *D);
  void VisitParmVarDecl(ParmVarDecl *D);
  void VisitFileScopeAsmDecl(FileScopeAsmDecl *D);
  void VisitImportDecl(ImportDecl *D);
  void VisitStaticAssertDecl(StaticAssertDecl *D);
  void VisitNamespaceDecl(NamespaceDecl *D);
  void VisitUsingDirectiveDecl(UsingDirectiveDecl *D);
  void VisitNamespaceAliasDecl(NamespaceAliasDecl *D);
  void VisitCXXRecordDecl(CXXRecordDecl *D);
  void VisitLinkageSpecDecl(LinkageSpecDecl *D);
  void VisitTemplateDecl(const TemplateDecl *D);
  void VisitFunctionTemplateDecl(FunctionTemplateDecl *D);
  void VisitClassTemplateDecl(ClassTemplateDecl *D);
  void VisitClassTemplateSpecializationDecl(ClassTemplateSpecializationDecl *D);
  void VisitClassTemplatePartialSpecializationDecl(
      ClassTemplatePartialSpecializationDecl *D);
  void VisitObjCMethodDecl(ObjCMethodDecl *D);
  void VisitObjCImplementationDecl(ObjCImplementationDecl *D);
  void VisitObjCInterfaceDecl(ObjCInterfaceDecl *D);
  void VisitObjCProtocolDecl(ObjCProtocolDecl *D);
  void VisitObjCCategoryImplDecl(ObjCCategoryImplDecl *D);
  void VisitObjCCategoryDecl(ObjCCategoryDecl *D);
  void VisitObjCCompatibleAliasDecl(ObjCCompatibleAliasDecl *D);
  void VisitObjCPropertyDecl(ObjCPropertyDecl *D);
  void VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *D);
  void VisitUnresolvedUsingTypenameDecl(UnresolvedUsingTypenameDecl *D);
  void VisitUnresolvedUsingValueDecl(UnresolvedUsingValueDecl *D);
  void VisitUsingDecl(UsingDecl *D);
  void VisitUsingShadowDecl(UsingShadowDecl *D);
  void VisitOMPThreadPrivateDecl(OMPThreadPrivateDecl *D);
  void VisitOMPAllocateDecl(OMPAllocateDecl *D);
  void VisitOMPRequiresDecl(OMPRequiresDecl *D);
  void VisitOMPDeclareReductionDecl(OMPDeclareReductionDecl *D);
  void VisitOMPDeclareMapperDecl(OMPDeclareMapperDecl *D);
  void VisitOMPCapturedExprDecl(OMPCapturedExprDecl *D);
  void VisitTemplateTypeParmDecl(const TemplateTypeParmDecl *TTP);
  void VisitNonTypeTemplateParmDecl(const NonTypeTemplateParmDecl *NTTP);

  void printTemplateParameters(const TemplateParameterList *Params,
                               bool OmitTemplateKW = false);
  void printTemplateArguments(llvm::ArrayRef<TemplateArgument> Args);
  void printTemplateArguments(llvm::ArrayRef<TemplateArgumentLoc> Args);
  void prettyPrintAttributes(Decl *D);
  void prettyPrintPragmas(Decl *D);
  void printDeclType(QualType T, StringRef DeclName, bool Pack = false);

  void Visit(Decl *D);
};
}  // namespace

void DeclPrinter::Visit(Decl *D) {
  Out << "<span class=\"clang decl\" id=\"";
  Out.write_hex((unsigned long long)D);
  Out << "\">";
  DeclVisitor<DeclPrinter>::Visit(D);
  Out << "</span>";
}

void PrintDecl(clang::Decl *decl, const clang::PrintingPolicy &Policy,
               int Indentation, llvm::raw_ostream &Out) {
  auto &ast{decl->getASTContext()};
  DeclPrinter Printer(Out, Policy, ast, Indentation, false);
  Printer.Visit(decl);
}

static QualType GetBaseType(QualType T) {
  // FIXME: This should be on the Type class!
  QualType BaseType = T;
  while (!BaseType->isSpecifierType()) {
    if (const PointerType *PTy = BaseType->getAs<PointerType>())
      BaseType = PTy->getPointeeType();
    else if (const BlockPointerType *BPy = BaseType->getAs<BlockPointerType>())
      BaseType = BPy->getPointeeType();
    else if (const ArrayType *ATy = dyn_cast<ArrayType>(BaseType))
      BaseType = ATy->getElementType();
    else if (const FunctionType *FTy = BaseType->getAs<FunctionType>())
      BaseType = FTy->getReturnType();
    else if (const VectorType *VTy = BaseType->getAs<VectorType>())
      BaseType = VTy->getElementType();
    else if (const ReferenceType *RTy = BaseType->getAs<ReferenceType>())
      BaseType = RTy->getPointeeType();
    else if (const AutoType *ATy = BaseType->getAs<AutoType>())
      BaseType = ATy->getDeducedType();
    else if (const ParenType *PTy = BaseType->getAs<ParenType>())
      BaseType = PTy->desugar();
    else
      // This must be a syntax error.
      break;
  }
  return BaseType;
}

static QualType getDeclType(Decl *D) {
  if (TypedefNameDecl *TDD = dyn_cast<TypedefNameDecl>(D))
    return TDD->getUnderlyingType();
  if (ValueDecl *VD = dyn_cast<ValueDecl>(D)) return VD->getType();
  return QualType();
}

void PrintDeclGroup(Decl **Begin, unsigned NumDecls, raw_ostream &Out,
                    const PrintingPolicy &Policy, unsigned Indentation) {
  if (NumDecls == 1) {
    PrintDecl(*Begin, Policy, Indentation, Out);
    return;
  }

  Decl **End = Begin + NumDecls;
  TagDecl *TD = dyn_cast<TagDecl>(*Begin);
  if (TD) ++Begin;

  PrintingPolicy SubPolicy(Policy);

  bool isFirst = true;
  for (; Begin != End; ++Begin) {
    if (isFirst) {
      if (TD) SubPolicy.IncludeTagDefinition = true;
      SubPolicy.SuppressSpecifiers = false;
      isFirst = false;
    } else {
      if (!isFirst) Out << ", ";
      SubPolicy.IncludeTagDefinition = false;
      SubPolicy.SuppressSpecifiers = true;
    }

    PrintDecl(*Begin, SubPolicy, Indentation, Out);
  }
}

raw_ostream &DeclPrinter::Indent(unsigned Indentation) {
  for (unsigned i = 0; i != Indentation; ++i) Out << "  ";
  return Out;
}

void DeclPrinter::prettyPrintAttributes(Decl *D) {
  if (Policy.PolishForDeclaration) return;

  if (D->hasAttrs()) {
    AttrVec &Attrs = D->getAttrs();
    for (auto *A : Attrs) {
      if (A->isInherited() || A->isImplicit()) continue;
      switch (A->getKind()) {
#define ATTR(X)
#define PRAGMA_SPELLING_ATTR(X) case attr::X:
#include <clang/Basic/AttrList.inc>
        break;
        default:
          A->printPretty(Out, Policy);
          break;
      }
    }
  }
}

void DeclPrinter::prettyPrintPragmas(Decl *D) {
  if (Policy.PolishForDeclaration) return;

  if (D->hasAttrs()) {
    AttrVec &Attrs = D->getAttrs();
    for (auto *A : Attrs) {
      switch (A->getKind()) {
#define ATTR(X)
#define PRAGMA_SPELLING_ATTR(X) case attr::X:
#include <clang/Basic/AttrList.inc>
        A->printPretty(Out, Policy);
        Indent();
        break;
        default:
          break;
      }
    }
  }
}

void DeclPrinter::printDeclType(QualType T, StringRef DeclName, bool Pack) {
  // Normally, a PackExpansionType is written as T[3]... (for instance, as a
  // template argument), but if it is the type of a declaration, the ellipsis
  // is placed before the name being declared.
  if (auto *PET = T->getAs<PackExpansionType>()) {
    Pack = true;
    T = PET->getPattern();
  }
  PrintType(T, Out, Policy, (Pack ? "..." : "") + DeclName, Indentation);
}

void DeclPrinter::ProcessDeclGroup(SmallVectorImpl<Decl *> &Decls) {
  this->Indent();
  PrintDeclGroup(Decls.data(), Decls.size(), Out, Policy, Indentation);
  Out << ";\n";
  Decls.clear();
}

void DeclPrinter::Print(AccessSpecifier AS) {
  const auto AccessSpelling = getAccessSpelling(AS);
  if (AccessSpelling.empty()) llvm_unreachable("No access specifier!");
  Out << AccessSpelling;
}

void DeclPrinter::PrintConstructorInitializers(CXXConstructorDecl *CDecl,
                                               std::string &Proto) {
  bool HasInitializerList = false;
  for (const auto *BMInitializer : CDecl->inits()) {
    if (BMInitializer->isInClassMemberInitializer()) continue;

    if (!HasInitializerList) {
      Proto += " : ";
      Out << Proto;
      Proto.clear();
      HasInitializerList = true;
    } else
      Out << ", ";

    if (BMInitializer->isAnyMemberInitializer()) {
      FieldDecl *FD = BMInitializer->getAnyMember();
      Out << *FD;
    } else {
      Out << GetQualTypeAsString(QualType(BMInitializer->getBaseClass(), 0),
                                 Policy);
    }

    Out << "(";
    if (!BMInitializer->getInit()) {
      // Nothing to print
    } else {
      Expr *Init = BMInitializer->getInit();
      if (ExprWithCleanups *Tmp = dyn_cast<ExprWithCleanups>(Init))
        Init = Tmp->getSubExpr();

      Init = Init->IgnoreParens();

      Expr *SimpleInit = nullptr;
      Expr **Args = nullptr;
      unsigned NumArgs = 0;
      if (ParenListExpr *ParenList = dyn_cast<ParenListExpr>(Init)) {
        Args = ParenList->getExprs();
        NumArgs = ParenList->getNumExprs();
      } else if (CXXConstructExpr *Construct =
                     dyn_cast<CXXConstructExpr>(Init)) {
        Args = Construct->getArgs();
        NumArgs = Construct->getNumArgs();
      } else
        SimpleInit = Init;

      if (SimpleInit)
        PrintStmt(SimpleInit, Out, Policy, Indentation);
      else {
        for (unsigned I = 0; I != NumArgs; ++I) {
          assert(Args[I] != nullptr && "Expected non-null Expr");
          if (isa<CXXDefaultArgExpr>(Args[I])) break;

          if (I) Out << ", ";
          PrintStmt(Args[I], Out, Policy, Indentation);
        }
      }
    }
    Out << ")";
    if (BMInitializer->isPackExpansion()) Out << "...";
  }
}

//----------------------------------------------------------------------------
// Common C declarations
//----------------------------------------------------------------------------

void DeclPrinter::VisitDeclContext(DeclContext *DC, bool Indent) {
  if (Policy.TerseOutput) return;

  if (Indent) Indentation += Policy.Indentation;

  SmallVector<Decl *, 2> Decls;
  for (DeclContext::decl_iterator D = DC->decls_begin(), DEnd = DC->decls_end();
       D != DEnd; ++D) {
    // Don't print ObjCIvarDecls, as they are printed when visiting the
    // containing ObjCInterfaceDecl.
    if (isa<ObjCIvarDecl>(*D)) continue;

    // Skip over implicit declarations in pretty-printing mode.
    if (D->isImplicit()) continue;

    // Don't print implicit specializations, as they are printed when visiting
    // corresponding templates.
    if (auto FD = dyn_cast<FunctionDecl>(*D))
      if (FD->getTemplateSpecializationKind() == TSK_ImplicitInstantiation &&
          !isa<ClassTemplateSpecializationDecl>(DC))
        continue;

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
    QualType CurDeclType = getDeclType(*D);
    if (!Decls.empty() && !CurDeclType.isNull()) {
      QualType BaseType = GetBaseType(CurDeclType);
      if (!BaseType.isNull() && isa<ElaboratedType>(BaseType) &&
          cast<ElaboratedType>(BaseType)->getOwnedTagDecl() == Decls[0]) {
        Decls.push_back(*D);
        continue;
      }
    }

    // If we have a merged group waiting to be handled, handle it now.
    if (!Decls.empty()) ProcessDeclGroup(Decls);

    // If the current declaration is not a free standing declaration, save it
    // so we can merge it with the subsequent declaration(s) using it.
    if (isa<TagDecl>(*D) && !cast<TagDecl>(*D)->isFreeStanding()) {
      Decls.push_back(*D);
      continue;
    }

    if (isa<AccessSpecDecl>(*D)) {
      Indentation -= Policy.Indentation;
      this->Indent();
      Print(D->getAccess());
      Out << ":\n";
      Indentation += Policy.Indentation;
      continue;
    }

    this->Indent();
    Visit(*D);

    // FIXME: Need to be able to tell the DeclPrinter when
    const char *Terminator = nullptr;
    if (isa<OMPThreadPrivateDecl>(*D) || isa<OMPDeclareReductionDecl>(*D) ||
        isa<OMPDeclareMapperDecl>(*D) || isa<OMPRequiresDecl>(*D) ||
        isa<OMPAllocateDecl>(*D))
      Terminator = nullptr;
    else if (isa<ObjCMethodDecl>(*D) && cast<ObjCMethodDecl>(*D)->hasBody())
      Terminator = nullptr;
    else if (auto FD = dyn_cast<FunctionDecl>(*D)) {
      if (FD->isThisDeclarationADefinition())
        Terminator = nullptr;
      else
        Terminator = ";";
    } else if (auto TD = dyn_cast<FunctionTemplateDecl>(*D)) {
      if (TD->getTemplatedDecl()->isThisDeclarationADefinition())
        Terminator = nullptr;
      else
        Terminator = ";";
    } else if (isa<NamespaceDecl>(*D) || isa<LinkageSpecDecl>(*D) ||
               isa<ObjCImplementationDecl>(*D) || isa<ObjCInterfaceDecl>(*D) ||
               isa<ObjCProtocolDecl>(*D) || isa<ObjCCategoryImplDecl>(*D) ||
               isa<ObjCCategoryDecl>(*D))
      Terminator = nullptr;
    else if (isa<EnumConstantDecl>(*D)) {
      DeclContext::decl_iterator Next = D;
      ++Next;
      if (Next != DEnd) Terminator = ",";
    } else
      Terminator = ";";

    if (Terminator) Out << Terminator;
    if (!Policy.TerseOutput &&
        ((isa<FunctionDecl>(*D) &&
          cast<FunctionDecl>(*D)->doesThisDeclarationHaveABody()) ||
         (isa<FunctionTemplateDecl>(*D) &&
          cast<FunctionTemplateDecl>(*D)
              ->getTemplatedDecl()
              ->doesThisDeclarationHaveABody())))
      ;  // StmtPrinter already added '\n' after CompoundStmt.
    else
      Out << "\n";

    // Declare target attribute is special one, natural spelling for the pragma
    // assumes "ending" construct so print it here.
    if (D->hasAttr<OMPDeclareTargetDeclAttr>())
      Out << "<span class=\"clang preprocessor\">#pragma</span> omp end "
             "declare target\n";
  }

  if (!Decls.empty()) ProcessDeclGroup(Decls);

  if (Indent) Indentation -= Policy.Indentation;
}

void DeclPrinter::VisitTranslationUnitDecl(TranslationUnitDecl *D) {
  VisitDeclContext(D, false);
}

void DeclPrinter::VisitTypedefDecl(TypedefDecl *D) {
  if (!Policy.SuppressSpecifiers) {
    Out << "<span class=\"clang keyword\">typedef</span> ";

    if (D->isModulePrivate())
      Out << "<span class=\"clang keyword\">__module_private__</span> ";
  }
  QualType Ty = D->getTypeSourceInfo()->getType();
  PrintType(Ty, Out, Policy, D->getName(), Indentation);
  prettyPrintAttributes(D);
}

void DeclPrinter::VisitTypeAliasDecl(TypeAliasDecl *D) {
  Out << "<span class=\"clang keyword\">using</span> " << *D;
  prettyPrintAttributes(D);
  Out << " = "
      << GetQualTypeAsString(D->getTypeSourceInfo()->getType(), Policy);
}

void DeclPrinter::VisitEnumDecl(EnumDecl *D) {
  if (!Policy.SuppressSpecifiers && D->isModulePrivate())
    Out << "<span class=\"clang keyword\">__module_private__</span> ";
  Out << "<span class=\"clang keyword\">enum</span>";
  if (D->isScoped()) {
    if (D->isScopedUsingClassTag())
      Out << " <span class=\"clang keyword\">class</span>";
    else
      Out << " <span class=\"clang keyword\">struct</span>";
  }

  prettyPrintAttributes(D);

  if (D->getDeclName())
    Out << " <span class=\"clang typename\">" << D->getDeclName() << "</span>";

  if (D->isFixed()) {
    Out << " : ";
    PrintType(D->getIntegerType(), Out, Policy);
  }

  if (D->isCompleteDefinition()) {
    Out << " {\n";
    VisitDeclContext(D);
    Indent() << "}";
  }
}

void DeclPrinter::VisitRecordDecl(RecordDecl *D) {
  if (!Policy.SuppressSpecifiers && D->isModulePrivate())
    Out << "<span class=\"clang keyword\">__module_private__</span> ";
  Out << "<span class=\"clang keyword\">" << D->getKindName() << "</span>";

  prettyPrintAttributes(D);

  if (D->getIdentifier())
    Out << " <span class=\"clang typename\">" << *D << "</span>";

  if (D->isCompleteDefinition()) {
    Out << " {\n";
    VisitDeclContext(D);
    Indent() << "}";
  }
}

void DeclPrinter::VisitEnumConstantDecl(EnumConstantDecl *D) {
  Out << "<span class=\"clang enum-constant\">" << *D << "</span>";
  prettyPrintAttributes(D);
  if (Expr *Init = D->getInitExpr()) {
    Out << " = ";
    PrintStmt(Init, Out, Policy, Indentation, &Context);
  }
}

static void printExplicitSpecifier(ExplicitSpecifier ES, llvm::raw_ostream &Out,
                                   PrintingPolicy &Policy,
                                   unsigned Indentation) {
  std::string Proto = "<span class=\"clang keyword\">explicit</span>";
  llvm::raw_string_ostream EOut(Proto);
  if (ES.getExpr()) {
    EOut << "(";
    PrintStmt(ES.getExpr(), Out, Policy, Indentation);
    EOut << ")";
  }
  EOut << " ";
  EOut.flush();
  Out << EOut.str();
}

void DeclPrinter::VisitFunctionDecl(FunctionDecl *D) {
  if (!D->getDescribedFunctionTemplate() &&
      !D->isFunctionTemplateSpecialization())
    prettyPrintPragmas(D);

  if (D->isFunctionTemplateSpecialization())
    Out << "<span class=\"clang keyword\">template</span>&lt;&gt; ";
  else if (!D->getDescribedFunctionTemplate()) {
    for (unsigned I = 0, NumTemplateParams = D->getNumTemplateParameterLists();
         I < NumTemplateParams; ++I)
      printTemplateParameters(D->getTemplateParameterList(I));
  }

  CXXConstructorDecl *CDecl = dyn_cast<CXXConstructorDecl>(D);
  CXXConversionDecl *ConversionDecl = dyn_cast<CXXConversionDecl>(D);
  CXXDeductionGuideDecl *GuideDecl = dyn_cast<CXXDeductionGuideDecl>(D);
  if (!Policy.SuppressSpecifiers) {
    switch (D->getStorageClass()) {
      case SC_None:
        break;
      case SC_Extern:
        Out << "<span class=\"clang keyword\">extern</span> ";
        break;
      case SC_Static:
        Out << "<span class=\"clang keyword\">static</span> ";
        break;
      case SC_PrivateExtern:
        Out << "<span class=\"clang keyword\">__private_extern__</span> ";
        break;
      case SC_Auto:
      case SC_Register:
        llvm_unreachable("invalid for functions");
    }

    if (D->isInlineSpecified())
      Out << "<span class=\"clang keyword\">inline</span> ";
    if (D->isVirtualAsWritten())
      Out << "<span class=\"clang keyword\">virtual</span> ";
    if (D->isModulePrivate())
      Out << "<span class=\"clang keyword\">__module_private__</span> ";
    if (D->isConstexprSpecified() && !D->isExplicitlyDefaulted())
      Out << "<span class=\"clang keyword\">constexpr</span> ";
    if (D->isConsteval())
      Out << "<span class=\"clang keyword\">consteval</span> ";
    ExplicitSpecifier ExplicitSpec = ExplicitSpecifier::getFromDecl(D);
    if (ExplicitSpec.isSpecified())
      printExplicitSpecifier(ExplicitSpec, Out, Policy, Indentation);
  }

  PrintingPolicy SubPolicy(Policy);
  SubPolicy.SuppressSpecifiers = false;
  std::string Proto;

  if (Policy.FullyQualifiedName) {
    Proto += D->getQualifiedNameAsString();
  } else {
    llvm::raw_string_ostream OS(Proto);
    if (!Policy.SuppressScope) {
      if (const NestedNameSpecifier *NS = D->getQualifier()) {
        NS->print(OS, Policy);
      }
    }
    D->getNameInfo().printName(OS, Policy);
  }

  if (GuideDecl)
    Proto = GuideDecl->getDeducedTemplate()->getDeclName().getAsString();
  if (D->isFunctionTemplateSpecialization()) {
    llvm::raw_string_ostream POut(Proto);
    DeclPrinter TArgPrinter(POut, SubPolicy, Context, Indentation);
    const auto *TArgAsWritten = D->getTemplateSpecializationArgsAsWritten();
    if (TArgAsWritten && !Policy.PrintCanonicalTypes)
      TArgPrinter.printTemplateArguments(TArgAsWritten->arguments());
    else if (const TemplateArgumentList *TArgs =
                 D->getTemplateSpecializationArgs())
      TArgPrinter.printTemplateArguments(TArgs->asArray());
  }

  QualType Ty = D->getType();
  while (const ParenType *PT = dyn_cast<ParenType>(Ty)) {
    Proto = '(' + Proto + ')';
    Ty = PT->getInnerType();
  }

  if (const FunctionType *AFT = Ty->getAs<FunctionType>()) {
    const FunctionProtoType *FT = nullptr;
    if (D->hasWrittenPrototype()) FT = dyn_cast<FunctionProtoType>(AFT);

    Proto += "(";
    if (FT) {
      llvm::raw_string_ostream POut(Proto);
      DeclPrinter ParamPrinter(POut, SubPolicy, Context, Indentation);
      for (unsigned i = 0, e = D->getNumParams(); i != e; ++i) {
        if (i) POut << ", ";
        ParamPrinter.Visit(D->getParamDecl(i));
      }

      if (FT->isVariadic()) {
        if (D->getNumParams()) POut << ", ";
        POut << "...";
      } else if (!D->getNumParams() && !Context.getLangOpts().CPlusPlus) {
        // The function has a prototype, so it needs to retain the prototype
        // in C.
        POut << "<span class=\"clang keyword\">void</span>";
      }
    } else if (D->doesThisDeclarationHaveABody() && !D->hasPrototype()) {
      for (unsigned i = 0, e = D->getNumParams(); i != e; ++i) {
        if (i) Proto += ", ";
        Proto += D->getParamDecl(i)->getNameAsString();
      }
    }

    Proto += ")";

    if (FT) {
      if (FT->isConst()) Proto += " <span class=\"clang keyword\">const</span>";
      if (FT->isVolatile())
        Proto += " <span class=\"clang keyword\">volatile</span>";
      if (FT->isRestrict())
        Proto += " <span class=\"clang keyword\">restrict</span>";

      switch (FT->getRefQualifier()) {
        case RQ_None:
          break;
        case RQ_LValue:
          Proto += " &amp;";
          break;
        case RQ_RValue:
          Proto += " &amp;&amp;";
          break;
      }
    }

    if (FT && FT->hasDynamicExceptionSpec()) {
      Proto += " <span class=\"clang keyword\">throw</span>(";
      if (FT->getExceptionSpecType() == EST_MSAny)
        Proto += "...";
      else
        for (unsigned I = 0, N = FT->getNumExceptions(); I != N; ++I) {
          if (I) Proto += ", ";

          Proto += GetQualTypeAsString(FT->getExceptionType(I), SubPolicy);
        }
      Proto += ")";
    } else if (FT && isNoexceptExceptionSpec(FT->getExceptionSpecType())) {
      Proto += " <span class=\"clang keyword\">noexcept</span>";
      if (isComputedNoexcept(FT->getExceptionSpecType())) {
        Proto += "(";
        llvm::raw_string_ostream EOut(Proto);
        PrintStmt(FT->getNoexceptExpr(), EOut, SubPolicy, Indentation);
        EOut.flush();
        Proto += EOut.str();
        Proto += ")";
      }
    }

    if (CDecl) {
      if (!Policy.TerseOutput) PrintConstructorInitializers(CDecl, Proto);
    } else if (!ConversionDecl && !isa<CXXDestructorDecl>(D)) {
      if (FT && FT->hasTrailingReturn()) {
        if (!GuideDecl) Out << "<span class=\"clang keyword\">auto</span> ";
        Out << Proto << " -&gt; ";
        Proto.clear();
      }
      PrintType(AFT->getReturnType(), Out, Policy, Proto);
      Proto.clear();
    }
    Out << Proto;

    if (Expr *TrailingRequiresClause = D->getTrailingRequiresClause()) {
      Out << " <span class=\"clang keyword\">requires</span> ";
      PrintStmt(TrailingRequiresClause, Out, SubPolicy, Indentation);
    }
  } else {
    PrintType(Ty, Out, Policy, Proto);
  }

  prettyPrintAttributes(D);

  if (D->isPure())
    Out << " = 0";
  else if (D->isDeletedAsWritten())
    Out << " = <span class=\"clang keyword\">delete</span>";
  else if (D->isExplicitlyDefaulted())
    Out << " = <span class=\"clang keyword\">default</span>";
  else if (D->doesThisDeclarationHaveABody()) {
    if (!Policy.TerseOutput) {
      if (!D->hasPrototype() && D->getNumParams()) {
        // This is a K&R function definition, so we need to print the
        // parameters.
        Out << '\n';
        DeclPrinter ParamPrinter(Out, SubPolicy, Context, Indentation);
        Indentation += Policy.Indentation;
        for (unsigned i = 0, e = D->getNumParams(); i != e; ++i) {
          Indent();
          ParamPrinter.Visit(D->getParamDecl(i));
          Out << ";\n";
        }
        Indentation -= Policy.Indentation;
      } else
        Out << ' ';

      if (D->getBody()) PrintStmt(D->getBody(), Out, SubPolicy, Indentation);
    } else {
      if (!Policy.TerseOutput && isa<CXXConstructorDecl>(*D)) Out << " {}";
    }
  }
}

void DeclPrinter::VisitFriendDecl(FriendDecl *D) {
  if (TypeSourceInfo *TSI = D->getFriendType()) {
    unsigned NumTPLists = D->getFriendTypeNumTemplateParameterLists();
    for (unsigned i = 0; i < NumTPLists; ++i)
      printTemplateParameters(D->getFriendTypeTemplateParameterList(i));
    Out << "<span class=\"clang keyword\">friend</span> ";
    Out << " " << GetQualTypeAsString(TSI->getType(), Policy);
  } else if (FunctionDecl *FD = dyn_cast<FunctionDecl>(D->getFriendDecl())) {
    Out << "<span class=\"clang keyword\">friend</span> ";
    VisitFunctionDecl(FD);
  } else if (FunctionTemplateDecl *FTD =
                 dyn_cast<FunctionTemplateDecl>(D->getFriendDecl())) {
    Out << "<span class=\"clang keyword\">friend</span> ";
    VisitFunctionTemplateDecl(FTD);
  } else if (ClassTemplateDecl *CTD =
                 dyn_cast<ClassTemplateDecl>(D->getFriendDecl())) {
    Out << "<span class=\"clang keyword\">friend</span> ";
    VisitRedeclarableTemplateDecl(CTD);
  }
}

void DeclPrinter::VisitFieldDecl(FieldDecl *D) {
  // FIXME: add printing of pragma attributes if required.
  if (!Policy.SuppressSpecifiers && D->isMutable())
    Out << "<span class=\"clang keyword\">mutable</span> ";
  if (!Policy.SuppressSpecifiers && D->isModulePrivate())
    Out << "<span class=\"clang keyword\">__module_private__</span> ";

  PrintType(D->getASTContext().getUnqualifiedObjCPointerType(D->getType()), Out,
            Policy, D->getName(), Indentation);

  if (D->isBitField()) {
    Out << " : ";
    PrintStmt(D->getBitWidth(), Out, Policy, Indentation);
  }

  Expr *Init = D->getInClassInitializer();
  if (!Policy.SuppressInitializers && Init) {
    if (D->getInClassInitStyle() == ICIS_ListInit)
      Out << " ";
    else
      Out << " = ";
    PrintStmt(Init, Out, Policy, Indentation);
  }
  prettyPrintAttributes(D);
}

void DeclPrinter::VisitLabelDecl(LabelDecl *D) { Out << *D << ":"; }

void DeclPrinter::VisitVarDecl(VarDecl *D) {
  prettyPrintPragmas(D);

  QualType T =
      D->getTypeSourceInfo()
          ? D->getTypeSourceInfo()->getType()
          : D->getASTContext().getUnqualifiedObjCPointerType(D->getType());

  if (!Policy.SuppressSpecifiers) {
    StorageClass SC = D->getStorageClass();
    if (SC != SC_None)
      Out << VarDecl::getStorageClassSpecifierString(SC) << " ";

    switch (D->getTSCSpec()) {
      case TSCS_unspecified:
        break;
      case TSCS___thread:
        Out << "<span class=\"clang keyword\">__thread</span> ";
        break;
      case TSCS__Thread_local:
        Out << "<span class=\"clang keyword\">_Thread_local</span> ";
        break;
      case TSCS_thread_local:
        Out << "<span class=\"clang keyword\">thread_local</span> ";
        break;
    }

    if (D->isModulePrivate())
      Out << "<span class=\"clang keyword\">__module_private__</span> ";

    if (D->isConstexpr()) {
      Out << "<span class=\"clang keyword\">constexpr</span> ";
      T.removeLocalConst();
    }
  }

  printDeclType(T, D->getName());
  Expr *Init = D->getInit();
  if (!Policy.SuppressInitializers && Init) {
    bool ImplicitInit = false;
    if (D->isCXXForRangeDecl()) {
      // FIXME: We should print the range expression instead.
      ImplicitInit = true;
    } else if (CXXConstructExpr *Construct =
                   dyn_cast<CXXConstructExpr>(Init->IgnoreImplicit())) {
      if (D->getInitStyle() == VarDecl::CallInit &&
          !Construct->isListInitialization()) {
        ImplicitInit = Construct->getNumArgs() == 0 ||
                       Construct->getArg(0)->isDefaultArgument();
      }
    }
    if (!ImplicitInit) {
      if ((D->getInitStyle() == VarDecl::CallInit) && !isa<ParenListExpr>(Init))
        Out << "(";
      else if (D->getInitStyle() == VarDecl::CInit) {
        Out << " = ";
      }
      PrintingPolicy SubPolicy(Policy);
      SubPolicy.SuppressSpecifiers = false;
      SubPolicy.IncludeTagDefinition = false;
      PrintStmt(Init, Out, SubPolicy, Indentation);
      if ((D->getInitStyle() == VarDecl::CallInit) && !isa<ParenListExpr>(Init))
        Out << ")";
    }
  }
  prettyPrintAttributes(D);
}

void DeclPrinter::VisitParmVarDecl(ParmVarDecl *D) { VisitVarDecl(D); }

void DeclPrinter::VisitFileScopeAsmDecl(FileScopeAsmDecl *D) {
  Out << "<span class=\"clang keyword\">__asm</span> (";
  PrintStmt(D->getAsmString(), Out, Policy, Indentation);
  Out << ")";
}

void DeclPrinter::VisitImportDecl(ImportDecl *D) {
  Out << "<span class=\"clang keyword objective-c\">@import</span> "
      << D->getImportedModule()->getFullModuleName() << ";\n";
}

void DeclPrinter::VisitStaticAssertDecl(StaticAssertDecl *D) {
  Out << "<span class=\"clang keyword\">static_assert</span>(";
  PrintStmt(D->getAssertExpr(), Out, Policy, Indentation);
  if (StringLiteral *SL = D->getMessage()) {
    Out << ", ";
    PrintStmt(SL, Out, Policy, Indentation);
  }
  Out << ")";
}

//----------------------------------------------------------------------------
// C++ declarations
//----------------------------------------------------------------------------
void DeclPrinter::VisitNamespaceDecl(NamespaceDecl *D) {
  if (D->isInline()) Out << "<span class=\"clang keyword\">inline</span> ";

  Out << "<span class=\"clang keyword\">namespace</span> ";
  if (D->getDeclName())
    Out << "<span class=\"clang namespace\">" << D->getDeclName() << "</span> ";
  Out << "{\n";

  VisitDeclContext(D);
  Indent() << "}";
}

void DeclPrinter::VisitUsingDirectiveDecl(UsingDirectiveDecl *D) {
  Out << "<span class=\"clang keyword\">using</span> <span class=\"clang "
         "keyword\">namespace</span> ";
  if (D->getQualifier()) D->getQualifier()->print(Out, Policy);
  Out << *D->getNominatedNamespaceAsWritten();
}

void DeclPrinter::VisitNamespaceAliasDecl(NamespaceAliasDecl *D) {
  Out << "<span class=\"clang keyword\">namespace</span> " << *D << " = ";
  if (D->getQualifier()) D->getQualifier()->print(Out, Policy);
  Out << *D->getAliasedNamespace();
}

void DeclPrinter::VisitEmptyDecl(EmptyDecl *D) { prettyPrintAttributes(D); }

void DeclPrinter::VisitCXXRecordDecl(CXXRecordDecl *D) {
  // FIXME: add printing of pragma attributes if required.
  if (!Policy.SuppressSpecifiers && D->isModulePrivate())
    Out << "<span class=\"clang keyword\">__module_private__</span> ";
  Out << "<span class=\"clang keyword\">" << D->getKindName() << "</span>";

  prettyPrintAttributes(D);

  if (D->getIdentifier()) {
    Out << ' ' << *D;

    if (auto S = dyn_cast<ClassTemplateSpecializationDecl>(D)) {
      ArrayRef<TemplateArgument> Args = S->getTemplateArgs().asArray();
      if (!Policy.PrintCanonicalTypes)
        if (const auto *TSI = S->getTypeAsWritten())
          if (const auto *TST =
                  dyn_cast<TemplateSpecializationType>(TSI->getType()))
            Args = TST->template_arguments();
      printTemplateArguments(Args);
    }
  }

  if (D->hasDefinition()) {
    if (D->hasAttr<FinalAttr>()) {
      Out << " final";
    }
  }

  if (D->isCompleteDefinition()) {
    // Print the base classes
    if (D->getNumBases()) {
      Out << " : ";
      for (CXXRecordDecl::base_class_iterator Base = D->bases_begin(),
                                              BaseEnd = D->bases_end();
           Base != BaseEnd; ++Base) {
        if (Base != D->bases_begin()) Out << ", ";

        if (Base->isVirtual())
          Out << "<span class=\"clang keyword\">virtual</span> ";

        AccessSpecifier AS = Base->getAccessSpecifierAsWritten();
        if (AS != AS_none) {
          Print(AS);
          Out << " ";
        }
        Out << GetQualTypeAsString(Base->getType(), Policy);

        if (Base->isPackExpansion()) Out << "...";
      }
    }

    // Print the class definition
    // FIXME: Doesn't print access specifiers, e.g., "public:"
    if (Policy.TerseOutput) {
      Out << " {}";
    } else {
      Out << " {\n";
      VisitDeclContext(D);
      Indent() << "}";
    }
  }
}

void DeclPrinter::VisitLinkageSpecDecl(LinkageSpecDecl *D) {
  const char *l;
  if (D->getLanguage() == LinkageSpecDecl::lang_c)
    l = "C";
  else {
    assert(D->getLanguage() == LinkageSpecDecl::lang_cxx &&
           "unknown language in linkage specification");
    l = "C++";
  }

  Out << "<span class=\"clang keyword\">extern</span> \"" << l << "\" ";
  if (D->hasBraces()) {
    Out << "{\n";
    VisitDeclContext(D);
    Indent() << "}";
  } else
    Visit(*D->decls_begin());
}

void DeclPrinter::printTemplateParameters(const TemplateParameterList *Params,
                                          bool OmitTemplateKW) {
  assert(Params);

  if (!OmitTemplateKW) Out << "<span class=\"clang keyword\">template</span> ";
  Out << "&lt;";

  bool NeedComma = false;
  for (const Decl *Param : *Params) {
    if (Param->isImplicit()) continue;

    if (NeedComma)
      Out << ", ";
    else
      NeedComma = true;

    if (const auto *TTP = dyn_cast<TemplateTypeParmDecl>(Param)) {
      VisitTemplateTypeParmDecl(TTP);
    } else if (auto NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param)) {
      VisitNonTypeTemplateParmDecl(NTTP);
    } else if (auto TTPD = dyn_cast<TemplateTemplateParmDecl>(Param)) {
      VisitTemplateDecl(TTPD);
      // FIXME: print the default argument, if present.
    }
  }

  Out << "&gt;";
  if (!OmitTemplateKW) Out << ' ';
}

void DeclPrinter::printTemplateArguments(ArrayRef<TemplateArgument> Args) {
  Out << "&lt;";
  for (size_t I = 0, E = Args.size(); I < E; ++I) {
    if (I) Out << ", ";
    Args[I].print(Policy, Out, true);
  }
  Out << "&gt;";
}

void DeclPrinter::printTemplateArguments(ArrayRef<TemplateArgumentLoc> Args) {
  Out << "&lt;";
  for (size_t I = 0, E = Args.size(); I < E; ++I) {
    if (I) Out << ", ";
    Args[I].getArgument().print(Policy, Out, true);
  }
  Out << "&gt;";
}

void DeclPrinter::VisitTemplateDecl(const TemplateDecl *D) {
  printTemplateParameters(D->getTemplateParameters());

  if (const TemplateTemplateParmDecl *TTP =
          dyn_cast<TemplateTemplateParmDecl>(D)) {
    Out << "<span class=\"clang keyword\">class</span>";

    if (TTP->isParameterPack())
      Out << " ...";
    else if (TTP->getDeclName())
      Out << ' ';

    if (TTP->getDeclName()) Out << TTP->getDeclName();
  } else if (auto *TD = D->getTemplatedDecl())
    Visit(TD);
  else if (const auto *Concept = dyn_cast<ConceptDecl>(D)) {
    Out << "<span class=\"clang keyword\">concept</span> " << Concept->getName()
        << " = ";
    PrintStmt(Concept->getConstraintExpr(), Out, Policy, Indentation);
    Out << ";";
  }
}

void DeclPrinter::VisitFunctionTemplateDecl(FunctionTemplateDecl *D) {
  prettyPrintPragmas(D->getTemplatedDecl());
  // Print any leading template parameter lists.
  if (const FunctionDecl *FD = D->getTemplatedDecl()) {
    for (unsigned I = 0, NumTemplateParams = FD->getNumTemplateParameterLists();
         I < NumTemplateParams; ++I)
      printTemplateParameters(FD->getTemplateParameterList(I));
  }
  VisitRedeclarableTemplateDecl(D);
  // Declare target attribute is special one, natural spelling for the pragma
  // assumes "ending" construct so print it here.
  if (D->getTemplatedDecl()->hasAttr<OMPDeclareTargetDeclAttr>())
    Out << "<span class=\"clang preprocessor\">#pragma</span> omp end declare "
           "target\n";

  // Never print "instantiations" for deduction guides (they don't really
  // have them).
  if (PrintInstantiation &&
      !isa<CXXDeductionGuideDecl>(D->getTemplatedDecl())) {
    FunctionDecl *PrevDecl = D->getTemplatedDecl();
    const FunctionDecl *Def;
    if (PrevDecl->isDefined(Def) && Def != PrevDecl) return;
    for (auto *I : D->specializations())
      if (I->getTemplateSpecializationKind() == TSK_ImplicitInstantiation) {
        if (!PrevDecl->isThisDeclarationADefinition()) Out << ";\n";
        Indent();
        prettyPrintPragmas(I);
        Visit(I);
      }
  }
}

void DeclPrinter::VisitClassTemplateDecl(ClassTemplateDecl *D) {
  VisitRedeclarableTemplateDecl(D);

  if (PrintInstantiation) {
    for (auto *I : D->specializations())
      if (I->getSpecializationKind() == TSK_ImplicitInstantiation) {
        if (D->isThisDeclarationADefinition()) Out << ";";
        Out << "\n";
        Visit(I);
      }
  }
}

void DeclPrinter::VisitClassTemplateSpecializationDecl(
    ClassTemplateSpecializationDecl *D) {
  Out << "<span class=\"clang keyword\">template</span>&lt;&gt; ";
  VisitCXXRecordDecl(D);
}

void DeclPrinter::VisitClassTemplatePartialSpecializationDecl(
    ClassTemplatePartialSpecializationDecl *D) {
  printTemplateParameters(D->getTemplateParameters());
  VisitCXXRecordDecl(D);
}

//----------------------------------------------------------------------------
// Objective-C declarations
//----------------------------------------------------------------------------

void DeclPrinter::PrintObjCMethodType(ASTContext &Ctx,
                                      Decl::ObjCDeclQualifier Quals,
                                      QualType T) {
  Out << '(';
  if (Quals & Decl::ObjCDeclQualifier::OBJC_TQ_In)
    Out << "<span class=\"clang keyword\">in</span> ";
  if (Quals & Decl::ObjCDeclQualifier::OBJC_TQ_Inout)
    Out << "<span class=\"clang keyword\">inout</span> ";
  if (Quals & Decl::ObjCDeclQualifier::OBJC_TQ_Out)
    Out << "<span class=\"clang keyword\">out</span> ";
  if (Quals & Decl::ObjCDeclQualifier::OBJC_TQ_Bycopy)
    Out << "<span class=\"clang keyword\">bycopy</span> ";
  if (Quals & Decl::ObjCDeclQualifier::OBJC_TQ_Byref)
    Out << "<span class=\"clang keyword\">byref</span> ";
  if (Quals & Decl::ObjCDeclQualifier::OBJC_TQ_Oneway)
    Out << "<span class=\"clang keyword\">oneway</span> ";
  if (Quals & Decl::ObjCDeclQualifier::OBJC_TQ_CSNullability) {
    if (auto nullability = AttributedType::stripOuterNullability(T))
      Out << getNullabilitySpelling(*nullability, true) << ' ';
  }

  Out << GetQualTypeAsString(Ctx.getUnqualifiedObjCPointerType(T), Policy);
  Out << ')';
}

void DeclPrinter::PrintObjCTypeParams(ObjCTypeParamList *Params) {
  Out << "&lt;";
  unsigned First = true;
  for (auto *Param : *Params) {
    if (First) {
      First = false;
    } else {
      Out << ", ";
    }

    switch (Param->getVariance()) {
      case ObjCTypeParamVariance::Invariant:
        break;

      case ObjCTypeParamVariance::Covariant:
        Out << "<span class=\"clang keyword\">__covariant</span> ";
        break;

      case ObjCTypeParamVariance::Contravariant:
        Out << "<span class=\"clang keyword\">__contravariant</span> ";
        break;
    }

    Out << Param->getDeclName();

    if (Param->hasExplicitBound()) {
      Out << " : " << GetQualTypeAsString(Param->getUnderlyingType(), Policy);
    }
  }
  Out << "&gt;";
}

void DeclPrinter::VisitObjCMethodDecl(ObjCMethodDecl *OMD) {
  if (OMD->isInstanceMethod())
    Out << "- ";
  else
    Out << "+ ";
  if (!OMD->getReturnType().isNull()) {
    PrintObjCMethodType(OMD->getASTContext(), OMD->getObjCDeclQualifier(),
                        OMD->getReturnType());
  }

  std::string name = OMD->getSelector().getAsString();
  std::string::size_type pos, lastPos = 0;
  for (const auto *PI : OMD->parameters()) {
    // FIXME: selector is missing here!
    pos = name.find_first_of(':', lastPos);
    if (lastPos != 0) Out << " ";
    Out << name.substr(lastPos, pos - lastPos) << ':';
    PrintObjCMethodType(OMD->getASTContext(), PI->getObjCDeclQualifier(),
                        PI->getType());
    Out << *PI;
    lastPos = pos + 1;
  }

  if (OMD->param_begin() == OMD->param_end()) Out << name;

  if (OMD->isVariadic()) Out << ", ...";

  prettyPrintAttributes(OMD);

  if (OMD->getBody() && !Policy.TerseOutput) {
    Out << ' ';
    PrintStmt(OMD->getBody(), Out, Policy, Indentation);
  } else if (Policy.PolishForDeclaration)
    Out << ';';
}

void DeclPrinter::VisitObjCImplementationDecl(ObjCImplementationDecl *OID) {
  std::string I = OID->getNameAsString();
  ObjCInterfaceDecl *SID = OID->getSuperClass();

  bool eolnOut = false;
  if (SID)
    Out << "<span class=\"clang keyword objective-c\">@implementation</span> "
        << I << " : " << *SID;
  else
    Out << "<span class=\"clang keyword objective-c\">@implementation</span> "
        << I;

  if (OID->ivar_size() > 0) {
    Out << "{\n";
    eolnOut = true;
    Indentation += Policy.Indentation;
    for (const auto *I : OID->ivars()) {
      Indent() << GetQualTypeAsString(
                      I->getASTContext().getUnqualifiedObjCPointerType(
                          I->getType()),
                      Policy)
               << ' ' << *I << ";\n";
    }
    Indentation -= Policy.Indentation;
    Out << "}\n";
  } else if (SID || (OID->decls_begin() != OID->decls_end())) {
    Out << "\n";
    eolnOut = true;
  }
  VisitDeclContext(OID, false);
  if (!eolnOut) Out << "\n";
  Out << "<span class=\"clang keyword objective-c\">@end</span>";
}

void DeclPrinter::VisitObjCInterfaceDecl(ObjCInterfaceDecl *OID) {
  std::string I = OID->getNameAsString();
  ObjCInterfaceDecl *SID = OID->getSuperClass();

  if (!OID->isThisDeclarationADefinition()) {
    Out << "<span class=\"clang keyword objective-c\">@class</span> " << I;

    if (auto TypeParams = OID->getTypeParamListAsWritten()) {
      PrintObjCTypeParams(TypeParams);
    }

    Out << ";";
    return;
  }
  bool eolnOut = false;
  Out << "<span class=\"clang keyword objective-c\">@interface</span> " << I;

  if (auto TypeParams = OID->getTypeParamListAsWritten()) {
    PrintObjCTypeParams(TypeParams);
  }

  if (SID)
    Out << " : "
        << GetQualTypeAsString(QualType(OID->getSuperClassType(), 0), Policy);

  // Protocols?
  const ObjCList<ObjCProtocolDecl> &Protocols = OID->getReferencedProtocols();
  if (!Protocols.empty()) {
    for (ObjCList<ObjCProtocolDecl>::iterator I = Protocols.begin(),
                                              E = Protocols.end();
         I != E; ++I)
      Out << (I == Protocols.begin() ? "&lt;" : ",") << **I;
    Out << "&gt; ";
  }

  if (OID->ivar_size() > 0) {
    Out << "{\n";
    eolnOut = true;
    Indentation += Policy.Indentation;
    for (const auto *I : OID->ivars()) {
      Indent() << GetQualTypeAsString(
                      I->getASTContext().getUnqualifiedObjCPointerType(
                          I->getType()),
                      Policy)
               << ' ' << *I << ";\n";
    }
    Indentation -= Policy.Indentation;
    Out << "}\n";
  } else if (SID || (OID->decls_begin() != OID->decls_end())) {
    Out << "\n";
    eolnOut = true;
  }

  VisitDeclContext(OID, false);
  if (!eolnOut) Out << "\n";
  Out << "<span class=\"clang keyword objective-c\">@end</span>";
  // FIXME: implement the rest...
}

void DeclPrinter::VisitObjCProtocolDecl(ObjCProtocolDecl *PID) {
  if (!PID->isThisDeclarationADefinition()) {
    Out << "<span class=\"clang keyword objective-c\">@protocol</span> " << *PID
        << ";\n";
    return;
  }
  // Protocols?
  const ObjCList<ObjCProtocolDecl> &Protocols = PID->getReferencedProtocols();
  if (!Protocols.empty()) {
    Out << "<span class=\"clang keyword objective-c\">@protocol</span> "
        << *PID;
    for (ObjCList<ObjCProtocolDecl>::iterator I = Protocols.begin(),
                                              E = Protocols.end();
         I != E; ++I)
      Out << (I == Protocols.begin() ? "&lt;" : ",") << **I;
    Out << "&gt;\n";
  } else
    Out << "<span class=\"clang keyword objective-c\">@protocol</span> " << *PID
        << '\n';
  VisitDeclContext(PID, false);
  Out << "<span class=\"clang keyword objective-c\">@end</span>";
}

void DeclPrinter::VisitObjCCategoryImplDecl(ObjCCategoryImplDecl *PID) {
  Out << "<span class=\"clang keyword objective-c\">@implementation</span> ";
  if (const auto *CID = PID->getClassInterface())
    Out << *CID;
  else
    Out << "&lt;&lt;error-type&gt;&gt;";
  Out << '(' << *PID << ")\n";

  VisitDeclContext(PID, false);
  Out << "<span class=\"clang keyword objective-c\">@end</span>";
  // FIXME: implement the rest...
}

void DeclPrinter::VisitObjCCategoryDecl(ObjCCategoryDecl *PID) {
  Out << "@interface ";
  if (const auto *CID = PID->getClassInterface())
    Out << *CID;
  else
    Out << "&lt;&lt;error-type&gt;&gt;";
  if (auto TypeParams = PID->getTypeParamList()) {
    PrintObjCTypeParams(TypeParams);
  }
  Out << "(" << *PID << ")\n";
  if (PID->ivar_size() > 0) {
    Out << "{\n";
    Indentation += Policy.Indentation;
    for (const auto *I : PID->ivars())
      Indent() << GetQualTypeAsString(
                      I->getASTContext().getUnqualifiedObjCPointerType(
                          I->getType()),
                      Policy)
               << ' ' << *I << ";\n";
    Indentation -= Policy.Indentation;
    Out << "}\n";
  }

  VisitDeclContext(PID, false);
  Out << "<span class=\"clang keyword objective-c\">@end</span>";

  // FIXME: implement the rest...
}

void DeclPrinter::VisitObjCCompatibleAliasDecl(ObjCCompatibleAliasDecl *AID) {
  Out << "<span class=\"clang keyword "
         "objective-c\">@compatibility_alias</span> "
      << *AID << ' ' << *AID->getClassInterface() << ";\n";
}

/// PrintObjCPropertyDecl - print a property declaration.
///
/// Print attributes in the following order:
/// - class
/// - nonatomic | atomic
/// - assign | retain | strong | copy | weak | unsafe_unretained
/// - readwrite | readonly
/// - getter & setter
/// - nullability
void DeclPrinter::VisitObjCPropertyDecl(ObjCPropertyDecl *PDecl) {
  if (PDecl->getPropertyImplementation() == ObjCPropertyDecl::Required)
    Out << "<span class=\"clang keyword objective-c\">@required</span>\n";
  else if (PDecl->getPropertyImplementation() == ObjCPropertyDecl::Optional)
    Out << "<span class=\"clang keyword objective-c\">@optional</span>\n";

  QualType T = PDecl->getType();

  Out << "<span class=\"clang keyword objective-c\">@property</span>";
  if (PDecl->getPropertyAttributes() != ObjCPropertyAttribute::kind_noattr) {
    bool first = true;
    Out << "(";
    if (PDecl->getPropertyAttributes() & ObjCPropertyAttribute::kind_class) {
      Out << (first ? "" : ", ")
          << "<span class=\"clang keyword\">class</span>";
      first = false;
    }

    if (PDecl->getPropertyAttributes() & ObjCPropertyAttribute::kind_direct) {
      Out << (first ? "" : ", ")
          << "<span class=\"clang keyword\">direct</span>";
      first = false;
    }

    if (PDecl->getPropertyAttributes() &
        ObjCPropertyAttribute::kind_nonatomic) {
      Out << (first ? "" : ", ")
          << "<span class=\"clang keyword\">nonatomic</span>";
      first = false;
    }
    if (PDecl->getPropertyAttributes() & ObjCPropertyAttribute::kind_atomic) {
      Out << (first ? "" : ", ")
          << "<span class=\"clang keyword\">atomic</span>";
      first = false;
    }

    if (PDecl->getPropertyAttributes() & ObjCPropertyAttribute::kind_assign) {
      Out << (first ? "" : ", ")
          << "<span class=\"clang keyword\">assign</span>";
      first = false;
    }
    if (PDecl->getPropertyAttributes() & ObjCPropertyAttribute::kind_retain) {
      Out << (first ? "" : ", ")
          << "<span class=\"clang keyword\">retain</span>";
      first = false;
    }

    if (PDecl->getPropertyAttributes() & ObjCPropertyAttribute::kind_strong) {
      Out << (first ? "" : ", ")
          << "<span class=\"clang keyword\">strong</span>";
      first = false;
    }
    if (PDecl->getPropertyAttributes() & ObjCPropertyAttribute::kind_copy) {
      Out << (first ? "" : ", ") << "<span class=\"clang keyword\">copy</span>";
      first = false;
    }
    if (PDecl->getPropertyAttributes() & ObjCPropertyAttribute::kind_weak) {
      Out << (first ? "" : ", ") << "<span class=\"clang keyword\">weak</span>";
      first = false;
    }
    if (PDecl->getPropertyAttributes() &
        ObjCPropertyAttribute::kind_unsafe_unretained) {
      Out << (first ? "" : ", ")
          << "<span class=\"clang keyword\">unsafe_unretained</span>";
      first = false;
    }

    if (PDecl->getPropertyAttributes() &
        ObjCPropertyAttribute::kind_readwrite) {
      Out << (first ? "" : ", ")
          << "<span class=\"clang keyword\">readwrite</span>";
      first = false;
    }
    if (PDecl->getPropertyAttributes() & ObjCPropertyAttribute::kind_readonly) {
      Out << (first ? "" : ", ")
          << "<span class=\"clang keyword\">readonly</span>";
      first = false;
    }

    if (PDecl->getPropertyAttributes() & ObjCPropertyAttribute::kind_getter) {
      Out << (first ? "" : ", ")
          << "<span class=\"clang keyword\">getter</span> = ";
      PDecl->getGetterName().print(Out);
      first = false;
    }
    if (PDecl->getPropertyAttributes() & ObjCPropertyAttribute::kind_setter) {
      Out << (first ? "" : ", ")
          << "<span class=\"clang keyword\">setter</span> = ";
      PDecl->getSetterName().print(Out);
      first = false;
    }

    if (PDecl->getPropertyAttributes() &
        ObjCPropertyAttribute::kind_nullability) {
      if (auto nullability = AttributedType::stripOuterNullability(T)) {
        if (*nullability == NullabilityKind::Unspecified &&
            (PDecl->getPropertyAttributes() &
             ObjCPropertyAttribute::kind_null_resettable)) {
          Out << (first ? "" : ", ")
              << "<span class=\"clang keyword\">null_resettable</span>";
        } else {
          Out << (first ? "" : ", ")
              << getNullabilitySpelling(*nullability, true);
        }
        first = false;
      }
    }

    (void)first;  // Silence dead store warning due to idiomatic code.
    Out << ")";
  }
  std::string TypeStr = GetQualTypeAsString(
      PDecl->getASTContext().getUnqualifiedObjCPointerType(T), Policy);
  Out << ' ' << TypeStr;
  if (!StringRef(TypeStr).endswith("*")) Out << ' ';
  Out << *PDecl;
  if (Policy.PolishForDeclaration) Out << ';';
}

void DeclPrinter::VisitObjCPropertyImplDecl(ObjCPropertyImplDecl *PID) {
  if (PID->getPropertyImplementation() == ObjCPropertyImplDecl::Synthesize)
    Out << "<span class=\"clang keyword objective-c\">@synthesize</span> ";
  else
    Out << "<span class=\"clang keyword objective-c\">@dynamic</span> ";
  Out << *PID->getPropertyDecl();
  if (PID->getPropertyIvarDecl()) Out << '=' << *PID->getPropertyIvarDecl();
}

void DeclPrinter::VisitUsingDecl(UsingDecl *D) {
  if (!D->isAccessDeclaration())
    Out << "<span class=\"clang keyword\">using</span> ";
  if (D->hasTypename()) Out << "<span class=\"clang keyword\">typename</span> ";
  D->getQualifier()->print(Out, Policy);

  // Use the correct record name when the using declaration is used for
  // inheriting constructors.
  for (const auto *Shadow : D->shadows()) {
    if (const auto *ConstructorShadow =
            dyn_cast<ConstructorUsingShadowDecl>(Shadow)) {
      assert(Shadow->getDeclContext() == ConstructorShadow->getDeclContext());
      Out << *ConstructorShadow->getNominatedBaseClass();
      return;
    }
  }
  Out << *D;
}

void DeclPrinter::VisitUnresolvedUsingTypenameDecl(
    UnresolvedUsingTypenameDecl *D) {
  Out << "<span class=\"clang keyword\">using</span> <span class=\"clang "
         "keyword\">typename</span> ";
  D->getQualifier()->print(Out, Policy);
  Out << D->getDeclName();
}

void DeclPrinter::VisitUnresolvedUsingValueDecl(UnresolvedUsingValueDecl *D) {
  if (!D->isAccessDeclaration())
    Out << "<span class=\"clang keyword\">using</span> ";
  D->getQualifier()->print(Out, Policy);
  Out << D->getDeclName();
}

void DeclPrinter::VisitUsingShadowDecl(UsingShadowDecl *D) {
  // ignore
}

void DeclPrinter::VisitOMPThreadPrivateDecl(OMPThreadPrivateDecl *D) {
  Out << "<span class=\"clang preprocessor\">#pragma</span> omp threadprivate";
  if (!D->varlist_empty()) {
    for (OMPThreadPrivateDecl::varlist_iterator I = D->varlist_begin(),
                                                E = D->varlist_end();
         I != E; ++I) {
      Out << (I == D->varlist_begin() ? '(' : ',');
      NamedDecl *ND = cast<DeclRefExpr>(*I)->getDecl();
      ND->printQualifiedName(Out);
    }
    Out << ")";
  }
}

void DeclPrinter::VisitOMPAllocateDecl(OMPAllocateDecl *D) {
  Out << "<span class=\"clang preprocessor\">#pragma</span> omp allocate";
  if (!D->varlist_empty()) {
    for (OMPAllocateDecl::varlist_iterator I = D->varlist_begin(),
                                           E = D->varlist_end();
         I != E; ++I) {
      Out << (I == D->varlist_begin() ? '(' : ',');
      NamedDecl *ND = cast<DeclRefExpr>(*I)->getDecl();
      ND->printQualifiedName(Out);
    }
    Out << ")";
  }
  if (!D->clauselist_empty()) {
    Out << " ";
    OMPClausePrinter Printer(Out, Policy);
    for (OMPClause *C : D->clauselists()) Printer.Visit(C);
  }
}

void DeclPrinter::VisitOMPRequiresDecl(OMPRequiresDecl *D) {
  Out << "<span class=\"clang preprocessor\">#pragma</span> omp requires ";
  if (!D->clauselist_empty()) {
    OMPClausePrinter Printer(Out, Policy);
    for (auto I = D->clauselist_begin(), E = D->clauselist_end(); I != E; ++I)
      Printer.Visit(*I);
  }
}

void DeclPrinter::VisitOMPDeclareReductionDecl(OMPDeclareReductionDecl *D) {
  if (!D->isInvalidDecl()) {
    Out << "<span class=\"clang preprocessor\">#pragma</span> omp declare "
           "reduction (";
    if (D->getDeclName().getNameKind() == DeclarationName::CXXOperatorName) {
      const char *OpName =
          getOperatorSpelling(D->getDeclName().getCXXOverloadedOperator());
      assert(OpName && "not an overloaded operator");
      Out << OpName;
    } else {
      assert(D->getDeclName().isIdentifier());
      D->printName(Out);
    }
    Out << " : ";
    PrintType(D->getType(), Out, Policy);
    Out << " : ";
    PrintStmt(D->getCombiner(), Out, Policy, 0);
    Out << ")";
    if (auto *Init = D->getInitializer()) {
      Out << " initializer(";
      switch (D->getInitializerKind()) {
        case OMPDeclareReductionDecl::DirectInit:
          Out << "omp_priv(";
          break;
        case OMPDeclareReductionDecl::CopyInit:
          Out << "omp_priv = ";
          break;
        case OMPDeclareReductionDecl::CallInit:
          break;
      }
      PrintStmt(Init, Out, Policy, 0);
      if (D->getInitializerKind() == OMPDeclareReductionDecl::DirectInit)
        Out << ")";
      Out << ")";
    }
  }
}

void DeclPrinter::VisitOMPDeclareMapperDecl(OMPDeclareMapperDecl *D) {
  if (!D->isInvalidDecl()) {
    Out << "<span class=\"clang preprocessor\">#pragma</span> omp declare "
           "mapper (";
    D->printName(Out);
    Out << " : ";
    PrintType(D->getType(), Out, Policy);
    Out << " ";
    Out << D->getVarName();
    Out << ")";
    if (!D->clauselist_empty()) {
      OMPClausePrinter Printer(Out, Policy);
      for (auto *C : D->clauselists()) {
        Out << " ";
        Printer.Visit(C);
      }
    }
  }
}

void DeclPrinter::VisitOMPCapturedExprDecl(OMPCapturedExprDecl *D) {
  PrintStmt(D->getInit(), Out, Policy, Indentation);
}

void DeclPrinter::VisitTemplateTypeParmDecl(const TemplateTypeParmDecl *TTP) {
  if (const TypeConstraint *TC = TTP->getTypeConstraint())
    TC->print(Out, Policy);
  else if (TTP->wasDeclaredWithTypename())
    Out << "<span class=\"clang keyword\">typename</span>";
  else
    Out << "<span class=\"clang keyword\">class</span>";

  if (TTP->isParameterPack())
    Out << " ...";
  else if (TTP->getDeclName())
    Out << ' ';

  if (TTP->getDeclName()) Out << TTP->getDeclName();

  if (TTP->hasDefaultArgument()) {
    Out << " = ";
    Out << GetQualTypeAsString(TTP->getDefaultArgument(), Policy);
  }
}

void DeclPrinter::VisitNonTypeTemplateParmDecl(
    const NonTypeTemplateParmDecl *NTTP) {
  StringRef Name;
  if (IdentifierInfo *II = NTTP->getIdentifier()) Name = II->getName();
  printDeclType(NTTP->getType(), Name, NTTP->isParameterPack());

  if (NTTP->hasDefaultArgument()) {
    Out << " = ";
    PrintStmt(NTTP->getDefaultArgument(), Out, Policy, Indentation);
  }
}
