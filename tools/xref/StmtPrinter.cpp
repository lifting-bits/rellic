//===- StmtPrinter.cpp - Printing implementation for Stmt ASTs ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Stmt::dumpPretty/Stmt::printPretty methods, which
// pretty print the AST back out to C code.
//
//===----------------------------------------------------------------------===//

#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/DeclBase.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclObjC.h>
#include <clang/AST/DeclOpenMP.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/ExprObjC.h>
#include <clang/AST/ExprOpenMP.h>
#include <clang/AST/NestedNameSpecifier.h>
#include <clang/AST/OpenMPClause.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/StmtCXX.h>
#include <clang/AST/StmtObjC.h>
#include <clang/AST/StmtOpenMP.h>
#include <clang/AST/StmtVisitor.h>
#include <clang/AST/TemplateBase.h>
#include <clang/AST/Type.h>
#include <clang/Basic/CharInfo.h>
#include <clang/Basic/ExpressionTraits.h>
#include <clang/Basic/IdentifierTable.h>
#include <clang/Basic/JsonSupport.h>
#include <clang/Basic/LLVM.h>
#include <clang/Basic/Lambda.h>
#include <clang/Basic/OpenMPKinds.h>
#include <clang/Basic/OperatorKinds.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/TypeTraits.h>
#include <clang/Lex/Lexer.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/Compiler.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/raw_ostream.h>

#include <cassert>
#include <string>

#include "Printer.h"

using namespace clang;

//===----------------------------------------------------------------------===//
// StmtPrinter Visitor
//===----------------------------------------------------------------------===//

namespace {

class StmtPrinter : public StmtVisitor<StmtPrinter> {
  raw_ostream &OS;
  unsigned IndentLevel;
  PrinterHelper *Helper;
  PrintingPolicy Policy;
  std::string NL;
  const ASTContext *Context;

 public:
  StmtPrinter(raw_ostream &os, PrinterHelper *helper,
              const PrintingPolicy &Policy, unsigned Indentation = 0,
              StringRef NL = "\n", const ASTContext *Context = nullptr)
      : OS(os),
        IndentLevel(Indentation),
        Helper(helper),
        Policy(Policy),
        NL(NL),
        Context(Context) {}

  void PrintStmt(Stmt *S) { PrintStmt(S, Policy.Indentation); }

  void PrintStmt(Stmt *S, int SubIndent) {
    IndentLevel += SubIndent;
    if (S && isa<Expr>(S)) {
      // If this is an expr used in a stmt context, indent and newline it.
      Indent();
      Visit(S);
      OS << ";" << NL;
    } else if (S) {
      Visit(S);
    } else {
      Indent() << "&lt;&lt;&lt;NULL STATEMENT&gt;&gt;&gt;" << NL;
    }
    IndentLevel -= SubIndent;
  }

  void PrintInitStmt(Stmt *S, unsigned PrefixWidth) {
    // FIXME: Cope better with odd prefix widths.
    IndentLevel += (PrefixWidth + 1) / 2;
    if (auto *DS = dyn_cast<DeclStmt>(S))
      PrintRawDeclStmt(DS);
    else
      PrintExpr(cast<Expr>(S));
    OS << "; ";
    IndentLevel -= (PrefixWidth + 1) / 2;
  }

  void PrintControlledStmt(Stmt *S) {
    if (auto *CS = dyn_cast<CompoundStmt>(S)) {
      OS << " ";
      PrintRawCompoundStmt(CS);
      OS << NL;
    } else {
      OS << NL;
      PrintStmt(S);
    }
  }

  void PrintRawCompoundStmt(CompoundStmt *S);
  void PrintRawDecl(Decl *D);
  void PrintRawDeclStmt(const DeclStmt *S);
  void PrintRawIfStmt(IfStmt *If);
  void PrintRawCXXCatchStmt(CXXCatchStmt *Catch);
  void PrintCallArgs(CallExpr *E);
  void PrintRawSEHExceptHandler(SEHExceptStmt *S);
  void PrintRawSEHFinallyStmt(SEHFinallyStmt *S);
  void PrintOMPExecutableDirective(OMPExecutableDirective *S,
                                   bool ForceNoStmt = false);

  void PrintExpr(Expr *E) {
    if (E)
      Visit(E);
    else
      OS << "&lt;null expr&gt;";
  }

  raw_ostream &Indent(int Delta = 0) {
    for (int i = 0, e = IndentLevel + Delta; i < e; ++i) OS << "  ";
    return OS;
  }

  void Visit(Stmt *S) {
    if (Helper && Helper->handledStmt(S, OS))
      return;
    else {
      OS << "<span class=\"clang stmt\" id=\"";
      OS.write_hex((unsigned long long)S);
      OS << "\">";
      StmtVisitor<StmtPrinter>::Visit(S);
      OS << "</span>";
    }
  }

  void VisitStmt(Stmt *Node) LLVM_ATTRIBUTE_UNUSED {
    Indent() << "&lt;&lt;unknown stmt type&gt;&gt;" << NL;
  }

  void VisitExpr(Expr *Node) LLVM_ATTRIBUTE_UNUSED {
    OS << "&lt;&lt;unknown expr type&gt;&gt;";
  }

  void VisitCXXNamedCastExpr(CXXNamedCastExpr *Node);

#define ABSTRACT_STMT(CLASS)
#define STMT(CLASS, PARENT) void Visit##CLASS(CLASS *Node);
#include <clang/AST/StmtNodes.inc>
};

}  // namespace

//===----------------------------------------------------------------------===//
//  Stmt printing methods.
//===----------------------------------------------------------------------===//

/// PrintRawCompoundStmt - Print a compound stmt without indenting the {, and
/// with no newline after the }.
void StmtPrinter::PrintRawCompoundStmt(CompoundStmt *Node) {
  OS << "{" << NL;
  for (auto *I : Node->body()) PrintStmt(I);

  Indent() << "}";
}

void StmtPrinter::PrintRawDecl(Decl *D) {
  PrintDecl(D, Policy, IndentLevel, OS);
}

void StmtPrinter::PrintRawDeclStmt(const DeclStmt *S) {
  SmallVector<Decl *, 2> Decls(S->decls());
  PrintDeclGroup(Decls.data(), Decls.size(), OS, Policy, IndentLevel);
}

void StmtPrinter::VisitNullStmt(NullStmt *Node) { Indent() << ";" << NL; }

void StmtPrinter::VisitDeclStmt(DeclStmt *Node) {
  Indent();
  PrintRawDeclStmt(Node);
  OS << ";" << NL;
}

void StmtPrinter::VisitCompoundStmt(CompoundStmt *Node) {
  Indent();
  PrintRawCompoundStmt(Node);
  OS << "" << NL;
}

void StmtPrinter::VisitCaseStmt(CaseStmt *Node) {
  Indent(-1) << "<span class=\"clang keyword\">case</span> ";
  PrintExpr(Node->getLHS());
  if (Node->getRHS()) {
    OS << " ... ";
    PrintExpr(Node->getRHS());
  }
  OS << ":" << NL;

  PrintStmt(Node->getSubStmt(), 0);
}

void StmtPrinter::VisitDefaultStmt(DefaultStmt *Node) {
  Indent(-1) << "<span class=\"clang keyword\">default</span>:" << NL;
  PrintStmt(Node->getSubStmt(), 0);
}

void StmtPrinter::VisitLabelStmt(LabelStmt *Node) {
  Indent(-1) << Node->getName() << ":" << NL;
  PrintStmt(Node->getSubStmt(), 0);
}

void StmtPrinter::VisitAttributedStmt(AttributedStmt *Node) {
  for (const auto *Attr : Node->getAttrs()) {
    Attr->printPretty(OS, Policy);
  }

  PrintStmt(Node->getSubStmt(), 0);
}

void StmtPrinter::PrintRawIfStmt(IfStmt *If) {
  OS << "<span class=\"clang keyword\">if</span> (";
  if (If->getInit()) PrintInitStmt(If->getInit(), 4);
  if (const DeclStmt *DS = If->getConditionVariableDeclStmt())
    PrintRawDeclStmt(DS);
  else
    PrintExpr(If->getCond());
  OS << ')';

  if (auto *CS = dyn_cast<CompoundStmt>(If->getThen())) {
    OS << ' ';
    PrintRawCompoundStmt(CS);
    OS << (If->getElse() ? " " : NL);
  } else {
    OS << NL;
    PrintStmt(If->getThen());
    if (If->getElse()) Indent();
  }

  if (Stmt *Else = If->getElse()) {
    OS << "<span class=\"clang keyword\">else</span>";

    if (auto *CS = dyn_cast<CompoundStmt>(Else)) {
      OS << ' ';
      PrintRawCompoundStmt(CS);
      OS << NL;
    } else if (auto *ElseIf = dyn_cast<IfStmt>(Else)) {
      OS << ' ';
      PrintRawIfStmt(ElseIf);
    } else {
      OS << NL;
      PrintStmt(If->getElse());
    }
  }
}

void StmtPrinter::VisitIfStmt(IfStmt *If) {
  Indent();
  PrintRawIfStmt(If);
}

void StmtPrinter::VisitSwitchStmt(SwitchStmt *Node) {
  Indent() << "<span class=\"clang keyword\">switch</span> (";
  if (Node->getInit()) PrintInitStmt(Node->getInit(), 8);
  if (const DeclStmt *DS = Node->getConditionVariableDeclStmt())
    PrintRawDeclStmt(DS);
  else
    PrintExpr(Node->getCond());
  OS << ")";
  PrintControlledStmt(Node->getBody());
}

void StmtPrinter::VisitWhileStmt(WhileStmt *Node) {
  Indent() << "<span class=\"clang keyword\">while</span> (";
  if (const DeclStmt *DS = Node->getConditionVariableDeclStmt())
    PrintRawDeclStmt(DS);
  else
    PrintExpr(Node->getCond());
  OS << ")" << NL;
  PrintStmt(Node->getBody());
}

void StmtPrinter::VisitDoStmt(DoStmt *Node) {
  Indent() << "<span class=\"clang keyword\">do</span> ";
  if (auto *CS = dyn_cast<CompoundStmt>(Node->getBody())) {
    PrintRawCompoundStmt(CS);
    OS << " ";
  } else {
    OS << NL;
    PrintStmt(Node->getBody());
    Indent();
  }

  OS << "<span class=\"clang keyword\">while</span> (";
  PrintExpr(Node->getCond());
  OS << ");" << NL;
}

void StmtPrinter::VisitForStmt(ForStmt *Node) {
  Indent() << "<span class=\"clang keyword\">for</span> (";
  if (Node->getInit())
    PrintInitStmt(Node->getInit(), 5);
  else
    OS << (Node->getCond() ? "; " : ";");
  if (Node->getCond()) PrintExpr(Node->getCond());
  OS << ";";
  if (Node->getInc()) {
    OS << " ";
    PrintExpr(Node->getInc());
  }
  OS << ")";
  PrintControlledStmt(Node->getBody());
}

void StmtPrinter::VisitObjCForCollectionStmt(ObjCForCollectionStmt *Node) {
  Indent() << "<span class=\"clang keyword\">for</span> (";
  if (auto *DS = dyn_cast<DeclStmt>(Node->getElement()))
    PrintRawDeclStmt(DS);
  else
    PrintExpr(cast<Expr>(Node->getElement()));
  OS << " <span class=\"clang keyword\">in</span> ";
  PrintExpr(Node->getCollection());
  OS << ")";
  PrintControlledStmt(Node->getBody());
}

void StmtPrinter::VisitCXXForRangeStmt(CXXForRangeStmt *Node) {
  Indent() << "<span class=\"clang keyword\">for</span> (";
  if (Node->getInit()) PrintInitStmt(Node->getInit(), 5);
  PrintingPolicy SubPolicy(Policy);
  SubPolicy.SuppressInitializers = true;
  PrintDecl(Node->getLoopVariable(), SubPolicy, IndentLevel, OS);
  OS << " : ";
  PrintExpr(Node->getRangeInit());
  OS << ")";
  PrintControlledStmt(Node->getBody());
}

void StmtPrinter::VisitMSDependentExistsStmt(MSDependentExistsStmt *Node) {
  Indent();
  if (Node->isIfExists())
    OS << "<span class=\"clang keyword\">__if_exists</span> (";
  else
    OS << "<span class=\"clang keyword\">__if_not_exists</span> (";

  if (NestedNameSpecifier *Qualifier =
          Node->getQualifierLoc().getNestedNameSpecifier())
    Qualifier->print(OS, Policy);

  OS << Node->getNameInfo() << ") ";

  PrintRawCompoundStmt(Node->getSubStmt());
}

void StmtPrinter::VisitGotoStmt(GotoStmt *Node) {
  Indent() << "<span class=\"clang keyword\">goto</span> "
           << Node->getLabel()->getName() << ";";
  if (Policy.IncludeNewlines) OS << NL;
}

void StmtPrinter::VisitIndirectGotoStmt(IndirectGotoStmt *Node) {
  Indent() << "<span class=\"clang keyword\">goto</span> *";
  PrintExpr(Node->getTarget());
  OS << ";";
  if (Policy.IncludeNewlines) OS << NL;
}

void StmtPrinter::VisitContinueStmt(ContinueStmt *Node) {
  Indent() << "<span class=\"clang keyword\">continue</span>;";
  if (Policy.IncludeNewlines) OS << NL;
}

void StmtPrinter::VisitBreakStmt(BreakStmt *Node) {
  Indent() << "<span class=\"clang keyword\">break</span>;";
  if (Policy.IncludeNewlines) OS << NL;
}

void StmtPrinter::VisitReturnStmt(ReturnStmt *Node) {
  Indent() << "<span class=\"clang keyword\">return</span>";
  if (Node->getRetValue()) {
    OS << " ";
    PrintExpr(Node->getRetValue());
  }
  OS << ";";
  if (Policy.IncludeNewlines) OS << NL;
}

void StmtPrinter::VisitGCCAsmStmt(GCCAsmStmt *Node) {
  Indent() << "<span class=\"clang keyword\">asm</span> ";

  if (Node->isVolatile())
    OS << "<span class=\"clang keyword\">volatile</span> ";

  if (Node->isAsmGoto()) OS << "<span class=\"clang keyword\">goto</span> ";

  OS << "(";
  VisitStringLiteral(Node->getAsmString());

  // Outputs
  if (Node->getNumOutputs() != 0 || Node->getNumInputs() != 0 ||
      Node->getNumClobbers() != 0 || Node->getNumLabels() != 0)
    OS << " : ";

  for (unsigned i = 0, e = Node->getNumOutputs(); i != e; ++i) {
    if (i != 0) OS << ", ";

    if (!Node->getOutputName(i).empty()) {
      OS << '[';
      OS << Node->getOutputName(i);
      OS << "] ";
    }

    VisitStringLiteral(Node->getOutputConstraintLiteral(i));
    OS << " (";
    Visit(Node->getOutputExpr(i));
    OS << ")";
  }

  // Inputs
  if (Node->getNumInputs() != 0 || Node->getNumClobbers() != 0 ||
      Node->getNumLabels() != 0)
    OS << " : ";

  for (unsigned i = 0, e = Node->getNumInputs(); i != e; ++i) {
    if (i != 0) OS << ", ";

    if (!Node->getInputName(i).empty()) {
      OS << '[';
      OS << Node->getInputName(i);
      OS << "] ";
    }

    VisitStringLiteral(Node->getInputConstraintLiteral(i));
    OS << " (";
    Visit(Node->getInputExpr(i));
    OS << ")";
  }

  // Clobbers
  if (Node->getNumClobbers() != 0 || Node->getNumLabels()) OS << " : ";

  for (unsigned i = 0, e = Node->getNumClobbers(); i != e; ++i) {
    if (i != 0) OS << ", ";

    VisitStringLiteral(Node->getClobberStringLiteral(i));
  }

  // Labels
  if (Node->getNumLabels() != 0) OS << " : ";

  for (unsigned i = 0, e = Node->getNumLabels(); i != e; ++i) {
    if (i != 0) OS << ", ";
    OS << Node->getLabelName(i);
  }

  OS << ");";
  if (Policy.IncludeNewlines) OS << NL;
}

void StmtPrinter::VisitMSAsmStmt(MSAsmStmt *Node) {
  // FIXME: Implement MS style inline asm statement printer.
  Indent() << "<span class=\"clang keyword\">__asm</span> ";
  if (Node->hasBraces()) OS << "{" << NL;
  OS << Node->getAsmString() << NL;
  if (Node->hasBraces()) Indent() << "}" << NL;
}

void StmtPrinter::VisitCapturedStmt(CapturedStmt *Node) {
  PrintStmt(Node->getCapturedDecl()->getBody());
}

void StmtPrinter::VisitObjCAtTryStmt(ObjCAtTryStmt *Node) {
  Indent() << "<span class=\"clang keyword objective-c\">@try</span>";
  if (auto *TS = dyn_cast<CompoundStmt>(Node->getTryBody())) {
    PrintRawCompoundStmt(TS);
    OS << NL;
  }

  for (unsigned I = 0, N = Node->getNumCatchStmts(); I != N; ++I) {
    ObjCAtCatchStmt *catchStmt = Node->getCatchStmt(I);
    Indent() << "<span class=\"clang keyword objective-c\">@catch</span>(";
    if (catchStmt->getCatchParamDecl()) {
      if (Decl *DS = catchStmt->getCatchParamDecl()) PrintRawDecl(DS);
    }
    OS << ")";
    if (auto *CS = dyn_cast<CompoundStmt>(catchStmt->getCatchBody())) {
      PrintRawCompoundStmt(CS);
      OS << NL;
    }
  }

  if (auto *FS = static_cast<ObjCAtFinallyStmt *>(Node->getFinallyStmt())) {
    Indent() << "<span class=\"clang keyword objective-c\">@finally</span>";
    PrintRawCompoundStmt(dyn_cast<CompoundStmt>(FS->getFinallyBody()));
    OS << NL;
  }
}

void StmtPrinter::VisitObjCAtFinallyStmt(ObjCAtFinallyStmt *Node) {}

void StmtPrinter::VisitObjCAtCatchStmt(ObjCAtCatchStmt *Node) {
  Indent() << "<span class=\"clang keyword objective-c\">@catch</span> (...) { "
              "/* todo */ } "
           << NL;
}

void StmtPrinter::VisitObjCAtThrowStmt(ObjCAtThrowStmt *Node) {
  Indent() << "<span class=\"clang keyword objective-c\">@throw</span>";
  if (Node->getThrowExpr()) {
    OS << " ";
    PrintExpr(Node->getThrowExpr());
  }
  OS << ";" << NL;
}

void StmtPrinter::VisitObjCAvailabilityCheckExpr(
    ObjCAvailabilityCheckExpr *Node) {
  OS << "<span class=\"clang keyword objective-c\">@available</span>(...)";
}

void StmtPrinter::VisitObjCAtSynchronizedStmt(ObjCAtSynchronizedStmt *Node) {
  Indent()
      << "<span class=\"clang keyword objective-c\">@synchronized</span> (";
  PrintExpr(Node->getSynchExpr());
  OS << ")";
  PrintRawCompoundStmt(Node->getSynchBody());
  OS << NL;
}

void StmtPrinter::VisitObjCAutoreleasePoolStmt(ObjCAutoreleasePoolStmt *Node) {
  Indent()
      << "<span class=\"clang keyword objective-c\">@autoreleasepool</span>";
  PrintRawCompoundStmt(dyn_cast<CompoundStmt>(Node->getSubStmt()));
  OS << NL;
}

void StmtPrinter::PrintRawCXXCatchStmt(CXXCatchStmt *Node) {
  OS << "<span class=\"clang keyword\">catch</span> (";
  if (Decl *ExDecl = Node->getExceptionDecl())
    PrintRawDecl(ExDecl);
  else
    OS << "...";
  OS << ") ";
  PrintRawCompoundStmt(cast<CompoundStmt>(Node->getHandlerBlock()));
}

void StmtPrinter::VisitCXXCatchStmt(CXXCatchStmt *Node) {
  Indent();
  PrintRawCXXCatchStmt(Node);
  OS << NL;
}

void StmtPrinter::VisitCXXTryStmt(CXXTryStmt *Node) {
  Indent() << "<span class=\"clang keyword\">try</span> ";
  PrintRawCompoundStmt(Node->getTryBlock());
  for (unsigned i = 0, e = Node->getNumHandlers(); i < e; ++i) {
    OS << " ";
    PrintRawCXXCatchStmt(Node->getHandler(i));
  }
  OS << NL;
}

void StmtPrinter::VisitSEHTryStmt(SEHTryStmt *Node) {
  Indent() << (Node->getIsCXXTry()
                   ? "<span class=\"clang keyword\">try</span> "
                   : "<span class=\"clang keyword\">__try</span> ");
  PrintRawCompoundStmt(Node->getTryBlock());
  SEHExceptStmt *E = Node->getExceptHandler();
  SEHFinallyStmt *F = Node->getFinallyHandler();
  if (E)
    PrintRawSEHExceptHandler(E);
  else {
    assert(F && "Must have a finally block...");
    PrintRawSEHFinallyStmt(F);
  }
  OS << NL;
}

void StmtPrinter::PrintRawSEHFinallyStmt(SEHFinallyStmt *Node) {
  OS << "<span class=\"clang keyword\">__finally</span> ";
  PrintRawCompoundStmt(Node->getBlock());
  OS << NL;
}

void StmtPrinter::PrintRawSEHExceptHandler(SEHExceptStmt *Node) {
  OS << "<span class=\"clang keyword\">__except</span> (";
  VisitExpr(Node->getFilterExpr());
  OS << ")" << NL;
  PrintRawCompoundStmt(Node->getBlock());
  OS << NL;
}

void StmtPrinter::VisitSEHExceptStmt(SEHExceptStmt *Node) {
  Indent();
  PrintRawSEHExceptHandler(Node);
  OS << NL;
}

void StmtPrinter::VisitSEHFinallyStmt(SEHFinallyStmt *Node) {
  Indent();
  PrintRawSEHFinallyStmt(Node);
  OS << NL;
}

void StmtPrinter::VisitSEHLeaveStmt(SEHLeaveStmt *Node) {
  Indent() << "<span class=\"clang keyword\">__leave</span>;";
  if (Policy.IncludeNewlines) OS << NL;
}

//===----------------------------------------------------------------------===//
//  OpenMP directives printing methods
//===----------------------------------------------------------------------===//

void StmtPrinter::VisitOMPCanonicalLoop(OMPCanonicalLoop *Node) {
  PrintStmt(Node->getLoopStmt());
}

void StmtPrinter::PrintOMPExecutableDirective(OMPExecutableDirective *S,
                                              bool ForceNoStmt) {
  OMPClausePrinter Printer(OS, Policy);
  ArrayRef<OMPClause *> Clauses = S->clauses();
  for (auto *Clause : Clauses)
    if (Clause && !Clause->isImplicit()) {
      OS << ' ';
      Printer.Visit(Clause);
    }
  OS << NL;
  if (!ForceNoStmt && S->hasAssociatedStmt()) PrintStmt(S->getRawStmt());
}

void StmtPrinter::VisitOMPParallelDirective(OMPParallelDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp parallel";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPSimdDirective(OMPSimdDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp simd";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTileDirective(OMPTileDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp tile";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPUnrollDirective(OMPUnrollDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp unroll";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPForDirective(OMPForDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp for";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPForSimdDirective(OMPForSimdDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp for simd";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPSectionsDirective(OMPSectionsDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp sections";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPSectionDirective(OMPSectionDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp section";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPSingleDirective(OMPSingleDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp single";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPMasterDirective(OMPMasterDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp master";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPCriticalDirective(OMPCriticalDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp critical";
  if (Node->getDirectiveName().getName()) {
    OS << " (";
    Node->getDirectiveName().printName(OS, Policy);
    OS << ")";
  }
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPParallelForDirective(OMPParallelForDirective *Node) {
  Indent()
      << "<span class=\"clang preprocessor\">#pragma</span> omp parallel for";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPParallelForSimdDirective(
    OMPParallelForSimdDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp parallel "
              "for simd";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPParallelMasterDirective(
    OMPParallelMasterDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp parallel "
              "master";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPParallelSectionsDirective(
    OMPParallelSectionsDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp parallel "
              "sections";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTaskDirective(OMPTaskDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp task";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTaskyieldDirective(OMPTaskyieldDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp taskyield";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPBarrierDirective(OMPBarrierDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp barrier";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTaskwaitDirective(OMPTaskwaitDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp taskwait";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTaskgroupDirective(OMPTaskgroupDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp taskgroup";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPFlushDirective(OMPFlushDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp flush";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPDepobjDirective(OMPDepobjDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp depobj";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPScanDirective(OMPScanDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp scan";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPOrderedDirective(OMPOrderedDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp ordered";
  PrintOMPExecutableDirective(Node, Node->hasClausesOfKind<OMPDependClause>());
}

void StmtPrinter::VisitOMPAtomicDirective(OMPAtomicDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp atomic";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTargetDirective(OMPTargetDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp target";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTargetDataDirective(OMPTargetDataDirective *Node) {
  Indent()
      << "<span class=\"clang preprocessor\">#pragma</span> omp target data";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTargetEnterDataDirective(
    OMPTargetEnterDataDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp target "
              "enter data";
  PrintOMPExecutableDirective(Node, /*ForceNoStmt=*/true);
}

void StmtPrinter::VisitOMPTargetExitDataDirective(
    OMPTargetExitDataDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp target "
              "exit data";
  PrintOMPExecutableDirective(Node, /*ForceNoStmt=*/true);
}

void StmtPrinter::VisitOMPTargetParallelDirective(
    OMPTargetParallelDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp target "
              "parallel";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTargetParallelForDirective(
    OMPTargetParallelForDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp target "
              "parallel for";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTeamsDirective(OMPTeamsDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp teams";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPCancellationPointDirective(
    OMPCancellationPointDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp "
              "cancellation point "
           << getOpenMPDirectiveName(Node->getCancelRegion());
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPCancelDirective(OMPCancelDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp cancel "
           << getOpenMPDirectiveName(Node->getCancelRegion());
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTaskLoopDirective(OMPTaskLoopDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp taskloop";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTaskLoopSimdDirective(
    OMPTaskLoopSimdDirective *Node) {
  Indent()
      << "<span class=\"clang preprocessor\">#pragma</span> omp taskloop simd";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPMasterTaskLoopDirective(
    OMPMasterTaskLoopDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp master "
              "taskloop";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPMasterTaskLoopSimdDirective(
    OMPMasterTaskLoopSimdDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp master "
              "taskloop simd";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPParallelMasterTaskLoopDirective(
    OMPParallelMasterTaskLoopDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp parallel "
              "master taskloop";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPParallelMasterTaskLoopSimdDirective(
    OMPParallelMasterTaskLoopSimdDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp parallel "
              "master taskloop simd";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPDistributeDirective(OMPDistributeDirective *Node) {
  Indent()
      << "<span class=\"clang preprocessor\">#pragma</span> omp distribute";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTargetUpdateDirective(
    OMPTargetUpdateDirective *Node) {
  Indent()
      << "<span class=\"clang preprocessor\">#pragma</span> omp target update";
  PrintOMPExecutableDirective(Node, /*ForceNoStmt=*/true);
}

void StmtPrinter::VisitOMPDistributeParallelForDirective(
    OMPDistributeParallelForDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp "
              "distribute parallel for";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPDistributeParallelForSimdDirective(
    OMPDistributeParallelForSimdDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp "
              "distribute parallel for simd";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPDistributeSimdDirective(
    OMPDistributeSimdDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp "
              "distribute simd";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTargetParallelForSimdDirective(
    OMPTargetParallelForSimdDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp target "
              "parallel for simd";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTargetSimdDirective(OMPTargetSimdDirective *Node) {
  Indent()
      << "<span class=\"clang preprocessor\">#pragma</span> omp target simd";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTeamsDistributeDirective(
    OMPTeamsDistributeDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp teams "
              "distribute";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTeamsDistributeSimdDirective(
    OMPTeamsDistributeSimdDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp teams "
              "distribute simd";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTeamsDistributeParallelForSimdDirective(
    OMPTeamsDistributeParallelForSimdDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp teams "
              "distribute parallel for simd";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTeamsDistributeParallelForDirective(
    OMPTeamsDistributeParallelForDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp teams "
              "distribute parallel for";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTargetTeamsDirective(OMPTargetTeamsDirective *Node) {
  Indent()
      << "<span class=\"clang preprocessor\">#pragma</span> omp target teams";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTargetTeamsDistributeDirective(
    OMPTargetTeamsDistributeDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp target "
              "teams distribute";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPInteropDirective(OMPInteropDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp interop";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPDispatchDirective(OMPDispatchDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp dispatch";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPMaskedDirective(OMPMaskedDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp masked";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTargetTeamsDistributeParallelForDirective(
    OMPTargetTeamsDistributeParallelForDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp target "
              "teams distribute parallel for";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTargetTeamsDistributeParallelForSimdDirective(
    OMPTargetTeamsDistributeParallelForSimdDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp target "
              "teams distribute parallel for simd";
  PrintOMPExecutableDirective(Node);
}

void StmtPrinter::VisitOMPTargetTeamsDistributeSimdDirective(
    OMPTargetTeamsDistributeSimdDirective *Node) {
  Indent() << "<span class=\"clang preprocessor\">#pragma</span> omp target "
              "teams distribute simd";
  PrintOMPExecutableDirective(Node);
}

//===----------------------------------------------------------------------===//
//  Expr printing methods.
//===----------------------------------------------------------------------===//

void StmtPrinter::VisitSourceLocExpr(SourceLocExpr *Node) {
  OS << Node->getBuiltinStr() << "()";
}

void StmtPrinter::VisitConstantExpr(ConstantExpr *Node) {
  PrintExpr(Node->getSubExpr());
}

void StmtPrinter::VisitDeclRefExpr(DeclRefExpr *Node) {
  if (const auto *OCED = dyn_cast<OMPCapturedExprDecl>(Node->getDecl())) {
    auto expr{const_cast<clang::Expr *>(OCED->getInit()->IgnoreImpCasts())};
    ::PrintStmt(expr, OS, Policy);
    return;
  }
  if (const auto *TPOD = dyn_cast<TemplateParamObjectDecl>(Node->getDecl())) {
    TPOD->printAsExpr(OS);
    return;
  }
  if (NestedNameSpecifier *Qualifier = Node->getQualifier())
    Qualifier->print(OS, Policy);
  if (Node->hasTemplateKeyword())
    OS << "<span class=\"clang keyword\">template</span> ";
  OS << Node->getNameInfo();
  if (Node->hasExplicitTemplateArgs())
    printTemplateArgumentList(OS, Node->template_arguments(), Policy);
}

void StmtPrinter::VisitDependentScopeDeclRefExpr(
    DependentScopeDeclRefExpr *Node) {
  if (NestedNameSpecifier *Qualifier = Node->getQualifier())
    Qualifier->print(OS, Policy);
  if (Node->hasTemplateKeyword())
    OS << "<span class=\"clang keyword\">template</span> ";
  OS << Node->getNameInfo();
  if (Node->hasExplicitTemplateArgs())
    printTemplateArgumentList(OS, Node->template_arguments(), Policy);
}

void StmtPrinter::VisitUnresolvedLookupExpr(UnresolvedLookupExpr *Node) {
  if (Node->getQualifier()) Node->getQualifier()->print(OS, Policy);
  if (Node->hasTemplateKeyword())
    OS << "<span class=\"clang keyword\">template</span> ";
  OS << Node->getNameInfo();
  if (Node->hasExplicitTemplateArgs())
    printTemplateArgumentList(OS, Node->template_arguments(), Policy);
}

static bool isImplicitSelf(const Expr *E) {
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const auto *PD = dyn_cast<ImplicitParamDecl>(DRE->getDecl())) {
      if (PD->getParameterKind() == ImplicitParamDecl::ObjCSelf &&
          DRE->getBeginLoc().isInvalid())
        return true;
    }
  }
  return false;
}

void StmtPrinter::VisitObjCIvarRefExpr(ObjCIvarRefExpr *Node) {
  if (Node->getBase()) {
    if (!Policy.SuppressImplicitBase ||
        !isImplicitSelf(Node->getBase()->IgnoreImpCasts())) {
      PrintExpr(Node->getBase());
      OS << (Node->isArrow() ? "-&gt;" : ".");
    }
  }
  OS << *Node->getDecl();
}

void StmtPrinter::VisitObjCPropertyRefExpr(ObjCPropertyRefExpr *Node) {
  if (Node->isSuperReceiver())
    OS << "<span class=\"clang keyword\">super</span>.";
  else if (Node->isObjectReceiver() && Node->getBase()) {
    PrintExpr(Node->getBase());
    OS << ".";
  } else if (Node->isClassReceiver() && Node->getClassReceiver()) {
    OS << Node->getClassReceiver()->getName() << ".";
  }

  if (Node->isImplicitProperty()) {
    if (const auto *Getter = Node->getImplicitPropertyGetter())
      Getter->getSelector().print(OS);
    else
      OS << SelectorTable::getPropertyNameFromSetterSelector(
          Node->getImplicitPropertySetter()->getSelector());
  } else
    OS << Node->getExplicitProperty()->getName();
}

void StmtPrinter::VisitObjCSubscriptRefExpr(ObjCSubscriptRefExpr *Node) {
  PrintExpr(Node->getBaseExpr());
  OS << "[";
  PrintExpr(Node->getKeyExpr());
  OS << "]";
}

void StmtPrinter::VisitSYCLUniqueStableNameExpr(
    SYCLUniqueStableNameExpr *Node) {
  OS << "__builtin_sycl_unique_stable_name(";
  Node->getTypeSourceInfo()->getType().print(OS, Policy);
  OS << ")";
}

void StmtPrinter::VisitPredefinedExpr(PredefinedExpr *Node) {
  OS << PredefinedExpr::getIdentKindName(Node->getIdentKind());
}

void StmtPrinter::VisitCharacterLiteral(CharacterLiteral *Node) {
  OS << "<span class=\"clang character-literal\">";
  unsigned value = Node->getValue();

  switch (Node->getKind()) {
    case CharacterLiteral::Ascii:
      break;  // no prefix.
    case CharacterLiteral::Wide:
      OS << 'L';
      break;
    case CharacterLiteral::UTF8:
      OS << "u8";
      break;
    case CharacterLiteral::UTF16:
      OS << 'u';
      break;
    case CharacterLiteral::UTF32:
      OS << 'U';
      break;
  }

  switch (value) {
    case '\\':
      OS << "'\\\\'";
      break;
    case '\'':
      OS << "'\\''";
      break;
    case '\a':
      // TODO: K&R: the meaning of '\\a' is different in traditional C
      OS << "'\\a'";
      break;
    case '\b':
      OS << "'\\b'";
      break;
    // Nonstandard escape sequence.
    /*case '\e':
      OS << "'\\e'";
      break;*/
    case '\f':
      OS << "'\\f'";
      break;
    case '\n':
      OS << "'\\n'";
      break;
    case '\r':
      OS << "'\\r'";
      break;
    case '\t':
      OS << "'\\t'";
      break;
    case '\v':
      OS << "'\\v'";
      break;
    case '<':
      OS << "&lt;";
      break;
    case '>':
      OS << "&gt;";
      break;
    case '&':
      OS << "&amp;";
      break;
    default:
      // A character literal might be sign-extended, which
      // would result in an invalid \U escape sequence.
      // FIXME: multicharacter literals such as '\xFF\xFF\xFF\xFF'
      // are not correctly handled.
      if ((value & ~0xFFu) == ~0xFFu &&
          Node->getKind() == CharacterLiteral::Ascii)
        value &= 0xFFu;
      if (value < 256 && isPrintable((unsigned char)value))
        OS << "'" << (char)value << "'";
      else if (value < 256)
        OS << "'\\x" << llvm::format("%02x", value) << "'";
      else if (value <= 0xFFFF)
        OS << "'\\u" << llvm::format("%04x", value) << "'";
      else
        OS << "'\\U" << llvm::format("%08x", value) << "'";
  }
  OS << "</span>";
}

/// Prints the given expression using the original source text. Returns true on
/// success, false otherwise.
static bool printExprAsWritten(raw_ostream &OS, Expr *E,
                               const ASTContext *Context) {
  if (!Context) return false;
  bool Invalid = false;
  StringRef Source = Lexer::getSourceText(
      CharSourceRange::getTokenRange(E->getSourceRange()),
      Context->getSourceManager(), Context->getLangOpts(), &Invalid);
  if (!Invalid) {
    OS << Source;
    return true;
  }
  return false;
}

void StmtPrinter::VisitIntegerLiteral(IntegerLiteral *Node) {
  if (Policy.ConstantsAsWritten && printExprAsWritten(OS, Node, Context))
    return;
  OS << "<span class=\"clang number integer-literal\">";
  bool isSigned = Node->getType()->isSignedIntegerType();
  OS << toString(Node->getValue(), 10, isSigned);

  // Emit suffixes.  Integer literals are always a builtin integer type.
  switch (Node->getType()->castAs<BuiltinType>()->getKind()) {
    default:
      llvm_unreachable("Unexpected type for integer literal!");
    case BuiltinType::Char_S:
    case BuiltinType::Char_U:
      OS << "i8";
      break;
    case BuiltinType::UChar:
      OS << "Ui8";
      break;
    case BuiltinType::Short:
      OS << "i16";
      break;
    case BuiltinType::UShort:
      OS << "Ui16";
      break;
    case BuiltinType::Int:
      break;  // no suffix.
    case BuiltinType::UInt:
      OS << 'U';
      break;
    case BuiltinType::Long:
      OS << 'L';
      break;
    case BuiltinType::ULong:
      OS << "UL";
      break;
    case BuiltinType::LongLong:
      OS << "LL";
      break;
    case BuiltinType::ULongLong:
      OS << "ULL";
      break;
  }
  OS << "</span>";
}

void StmtPrinter::VisitFixedPointLiteral(FixedPointLiteral *Node) {
  if (Policy.ConstantsAsWritten && printExprAsWritten(OS, Node, Context))
    return;
  OS << "<span class=\"clang number fixedpoint-literal\">";
  OS << Node->getValueAsString(/*Radix=*/10);

  switch (Node->getType()->castAs<BuiltinType>()->getKind()) {
    default:
      llvm_unreachable("Unexpected type for fixed point literal!");
    case BuiltinType::ShortFract:
      OS << "hr";
      break;
    case BuiltinType::ShortAccum:
      OS << "hk";
      break;
    case BuiltinType::UShortFract:
      OS << "uhr";
      break;
    case BuiltinType::UShortAccum:
      OS << "uhk";
      break;
    case BuiltinType::Fract:
      OS << "r";
      break;
    case BuiltinType::Accum:
      OS << "k";
      break;
    case BuiltinType::UFract:
      OS << "ur";
      break;
    case BuiltinType::UAccum:
      OS << "uk";
      break;
    case BuiltinType::LongFract:
      OS << "lr";
      break;
    case BuiltinType::LongAccum:
      OS << "lk";
      break;
    case BuiltinType::ULongFract:
      OS << "ulr";
      break;
    case BuiltinType::ULongAccum:
      OS << "ulk";
      break;
  }
  OS << "</span>";
}

static void PrintFloatingLiteral(raw_ostream &OS, FloatingLiteral *Node,
                                 bool PrintSuffix) {
  OS << "<span class=\"clang number floatingpoint-literal\">";
  SmallString<16> Str;
  Node->getValue().toString(Str);
  OS << Str;
  if (Str.find_first_not_of("-0123456789") == StringRef::npos)
    OS << '.';  // Trailing dot in order to separate from ints.

  if (!PrintSuffix) return;

  // Emit suffixes.  Float literals are always a builtin float type.
  switch (Node->getType()->castAs<BuiltinType>()->getKind()) {
    default:
      llvm_unreachable("Unexpected type for float literal!");
    case BuiltinType::Half:
      break;  // FIXME: suffix?
    case BuiltinType::Double:
      break;  // no suffix.
    case BuiltinType::Float16:
      OS << "F16";
      break;
    case BuiltinType::Float:
      OS << 'F';
      break;
    case BuiltinType::LongDouble:
      OS << 'L';
      break;
    case BuiltinType::Float128:
      OS << 'Q';
      break;
  }
  OS << "</span>";
}

void StmtPrinter::VisitFloatingLiteral(FloatingLiteral *Node) {
  if (Policy.ConstantsAsWritten && printExprAsWritten(OS, Node, Context))
    return;
  PrintFloatingLiteral(OS, Node, /*PrintSuffix=*/true);
}

void StmtPrinter::VisitImaginaryLiteral(ImaginaryLiteral *Node) {
  PrintExpr(Node->getSubExpr());
  OS << "i";
}

static void outputString(const StringLiteral *Str, raw_ostream &OS) {
  switch (Str->getKind()) {
    case StringLiteral::Ascii:
      break;  // no prefix.
    case StringLiteral::Wide:
      OS << 'L';
      break;
    case StringLiteral::UTF8:
      OS << "u8";
      break;
    case StringLiteral::UTF16:
      OS << 'u';
      break;
    case StringLiteral::UTF32:
      OS << 'U';
      break;
  }
  OS << '"';
  static const char Hex[] = "0123456789ABCDEF";
  unsigned LastSlashX = Str->getLength();
  for (unsigned I = 0, N = Str->getLength(); I != N; ++I) {
    switch (uint32_t Char = Str->getCodeUnit(I)) {
      default:
        // FIXME: Convert UTF-8 back to codepoints before rendering.
        // Convert UTF-16 surrogate pairs back to codepoints before rendering.
        // Leave invalid surrogates alone; we'll use \x for those.
        if (Str->getKind() == StringLiteral::UTF16 && I != N - 1 &&
            Char >= 0xd800 && Char <= 0xdbff) {
          uint32_t Trail = Str->getCodeUnit(I + 1);
          if (Trail >= 0xdc00 && Trail <= 0xdfff) {
            Char = 0x10000 + ((Char - 0xd800) << 10) + (Trail - 0xdc00);
            ++I;
          }
        }
        if (Char > 0xff) {
          // If this is a wide string, output characters over 0xff using \x
          // escapes. Otherwise, this is a UTF-16 or UTF-32 string, and Char is
          // a codepoint: use \x escapes for invalid codepoints.
          if (Str->getKind() == StringLiteral::Wide ||
              (Char >= 0xd800 && Char <= 0xdfff) || Char >= 0x110000) {
            // FIXME: Is this the best way to print wchar_t?
            OS << "\\x";
            int Shift = 28;
            while ((Char >> Shift) == 0) Shift -= 4;
            for (/**/; Shift >= 0; Shift -= 4) OS << Hex[(Char >> Shift) & 15];
            LastSlashX = I;
            break;
          }
          if (Char > 0xffff)
            OS << "\\U00" << Hex[(Char >> 20) & 15] << Hex[(Char >> 16) & 15];
          else
            OS << "\\u";
          OS << Hex[(Char >> 12) & 15] << Hex[(Char >> 8) & 15]
             << Hex[(Char >> 4) & 15] << Hex[(Char >> 0) & 15];
          break;
        }
        // If we used \x... for the previous character, and this character is a
        // hexadecimal digit, prevent it being slurped as part of the \x.
        if (LastSlashX + 1 == I) {
          switch (Char) {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case 'a':
            case 'b':
            case 'c':
            case 'd':
            case 'e':
            case 'f':
            case 'A':
            case 'B':
            case 'C':
            case 'D':
            case 'E':
            case 'F':
              OS << "\"\"";
          }
        }
        assert(Char <= 0xff &&
               "Characters above 0xff should already have been handled.");
        if (isPrintable(Char))
          OS << (char)Char;
        else  // Output anything hard as an octal escape.
          OS << '\\' << (char)('0' + ((Char >> 6) & 7))
             << (char)('0' + ((Char >> 3) & 7))
             << (char)('0' + ((Char >> 0) & 7));
        break;
      // Handle some common non-printable cases to make dumps prettier.
      case '\\':
        OS << "\\\\";
        break;
      case '"':
        OS << "\\\"";
        break;
      case '\a':
        OS << "\\a";
        break;
      case '\b':
        OS << "\\b";
        break;
      case '\f':
        OS << "\\f";
        break;
      case '\n':
        OS << "\\n";
        break;
      case '\r':
        OS << "\\r";
        break;
      case '\t':
        OS << "\\t";
        break;
      case '\v':
        OS << "\\v";
        break;
      case '<':
        OS << "&lt;";
        break;
      case '>':
        OS << "&gt;";
        break;
      case '&':
        OS << "&amp;";
        break;
    }
  }
  OS << '"';
}

void StmtPrinter::VisitStringLiteral(StringLiteral *Str) {
  OS << "<span class=\"clang string-literal\">";
  outputString(Str, OS);
  OS << "</span>";
}

void StmtPrinter::VisitParenExpr(ParenExpr *Node) {
  OS << "(";
  PrintExpr(Node->getSubExpr());
  OS << ")";
}

void StmtPrinter::VisitUnaryOperator(UnaryOperator *Node) {
  if (!Node->isPostfix()) {
    OS << UnaryOperator::getOpcodeStr(Node->getOpcode());

    // Print a space if this is an "identifier operator" like __real, or if
    // it might be concatenated incorrectly like '+'.
    switch (Node->getOpcode()) {
      default:
        break;
      case UO_Real:
      case UO_Imag:
      case UO_Extension:
        OS << ' ';
        break;
      case UO_Plus:
      case UO_Minus:
        if (isa<UnaryOperator>(Node->getSubExpr())) OS << ' ';
        break;
    }
  }
  PrintExpr(Node->getSubExpr());

  if (Node->isPostfix()) OS << UnaryOperator::getOpcodeStr(Node->getOpcode());
}

void StmtPrinter::VisitOffsetOfExpr(OffsetOfExpr *Node) {
  OS << "<span class=\"clang keyword\">__builtin_offsetof</span>(";
  PrintType(Node->getTypeSourceInfo()->getType(), OS, Policy);
  OS << ", ";
  bool PrintedSomething = false;
  for (unsigned i = 0, n = Node->getNumComponents(); i < n; ++i) {
    OffsetOfNode ON = Node->getComponent(i);
    if (ON.getKind() == OffsetOfNode::Array) {
      // Array node
      OS << "[";
      PrintExpr(Node->getIndexExpr(ON.getArrayExprIndex()));
      OS << "]";
      PrintedSomething = true;
      continue;
    }

    // Skip implicit base indirections.
    if (ON.getKind() == OffsetOfNode::Base) continue;

    // Field or identifier node.
    IdentifierInfo *Id = ON.getFieldName();
    if (!Id) continue;

    if (PrintedSomething)
      OS << ".";
    else
      PrintedSomething = true;
    OS << Id->getName();
  }
  OS << ")";
}

void StmtPrinter::VisitUnaryExprOrTypeTraitExpr(
    UnaryExprOrTypeTraitExpr *Node) {
  const char *Spelling = getTraitSpelling(Node->getKind());
  if (Node->getKind() == UETT_AlignOf) {
    if (Policy.Alignof)
      Spelling = "<span class=\"clang keyword\">alignof</span>";
    else if (Policy.UnderscoreAlignof)
      Spelling = "<span class=\"clang keyword\">_Alignof</span>";
    else
      Spelling = "<span class=\"clang keyword\">__alignof</span>";
  }

  OS << Spelling;

  if (Node->isArgumentType()) {
    OS << '(';
    PrintType(Node->getArgumentType(), OS, Policy);
    OS << ')';
  } else {
    OS << " ";
    PrintExpr(Node->getArgumentExpr());
  }
}

void StmtPrinter::VisitGenericSelectionExpr(GenericSelectionExpr *Node) {
  OS << "<span class=\"clang keyword\">_Generic</span>(";
  PrintExpr(Node->getControllingExpr());
  for (const GenericSelectionExpr::Association Assoc : Node->associations()) {
    OS << ", ";
    QualType T = Assoc.getType();
    if (T.isNull())
      OS << "<span class=\"clang keyword\">default</span>";
    else
      PrintType(T, OS, Policy);
    OS << ": ";
    PrintExpr(Assoc.getAssociationExpr());
  }
  OS << ")";
}

void StmtPrinter::VisitArraySubscriptExpr(ArraySubscriptExpr *Node) {
  PrintExpr(Node->getLHS());
  OS << "[";
  PrintExpr(Node->getRHS());
  OS << "]";
}

void StmtPrinter::VisitMatrixSubscriptExpr(MatrixSubscriptExpr *Node) {
  PrintExpr(Node->getBase());
  OS << "[";
  PrintExpr(Node->getRowIdx());
  OS << "]";
  OS << "[";
  PrintExpr(Node->getColumnIdx());
  OS << "]";
}

void StmtPrinter::VisitOMPArraySectionExpr(OMPArraySectionExpr *Node) {
  PrintExpr(Node->getBase());
  OS << "[";
  if (Node->getLowerBound()) PrintExpr(Node->getLowerBound());
  if (Node->getColonLocFirst().isValid()) {
    OS << ":";
    if (Node->getLength()) PrintExpr(Node->getLength());
  }
  if (Node->getColonLocSecond().isValid()) {
    OS << ":";
    if (Node->getStride()) PrintExpr(Node->getStride());
  }
  OS << "]";
}

void StmtPrinter::VisitOMPArrayShapingExpr(OMPArrayShapingExpr *Node) {
  OS << "(";
  for (Expr *E : Node->getDimensions()) {
    OS << "[";
    PrintExpr(E);
    OS << "]";
  }
  OS << ")";
  PrintExpr(Node->getBase());
}

void StmtPrinter::VisitOMPIteratorExpr(OMPIteratorExpr *Node) {
  OS << "iterator(";
  for (unsigned I = 0, E = Node->numOfIterators(); I < E; ++I) {
    auto *VD = cast<ValueDecl>(Node->getIteratorDecl(I));
    PrintType(VD->getType(), OS, Policy);
    const OMPIteratorExpr::IteratorRange Range = Node->getIteratorRange(I);
    OS << " " << VD->getName() << " = ";
    PrintExpr(Range.Begin);
    OS << ":";
    PrintExpr(Range.End);
    if (Range.Step) {
      OS << ":";
      PrintExpr(Range.Step);
    }
    if (I < E - 1) OS << ", ";
  }
  OS << ")";
}

void StmtPrinter::PrintCallArgs(CallExpr *Call) {
  for (unsigned i = 0, e = Call->getNumArgs(); i != e; ++i) {
    if (isa<CXXDefaultArgExpr>(Call->getArg(i))) {
      // Don't print any defaulted arguments
      break;
    }

    if (i) OS << ", ";
    PrintExpr(Call->getArg(i));
  }
}

void StmtPrinter::VisitCallExpr(CallExpr *Call) {
  PrintExpr(Call->getCallee());
  OS << "(";
  PrintCallArgs(Call);
  OS << ")";
}

static bool isImplicitThis(const Expr *E) {
  if (const auto *TE = dyn_cast<CXXThisExpr>(E)) return TE->isImplicit();
  return false;
}

void StmtPrinter::VisitMemberExpr(MemberExpr *Node) {
  if (!Policy.SuppressImplicitBase || !isImplicitThis(Node->getBase())) {
    PrintExpr(Node->getBase());

    auto *ParentMember = dyn_cast<MemberExpr>(Node->getBase());
    FieldDecl *ParentDecl =
        ParentMember ? dyn_cast<FieldDecl>(ParentMember->getMemberDecl())
                     : nullptr;

    if (!ParentDecl || !ParentDecl->isAnonymousStructOrUnion())
      OS << (Node->isArrow() ? "-&gt;" : ".");
  }

  if (auto *FD = dyn_cast<FieldDecl>(Node->getMemberDecl()))
    if (FD->isAnonymousStructOrUnion()) return;

  if (NestedNameSpecifier *Qualifier = Node->getQualifier())
    Qualifier->print(OS, Policy);
  if (Node->hasTemplateKeyword())
    OS << "<span class=\"clang keyword\">template</span> ";
  OS << Node->getMemberNameInfo();
  if (Node->hasExplicitTemplateArgs())
    printTemplateArgumentList(OS, Node->template_arguments(), Policy);
}

void StmtPrinter::VisitObjCIsaExpr(ObjCIsaExpr *Node) {
  PrintExpr(Node->getBase());
  OS << (Node->isArrow() ? "-&gt;isa" : ".isa");
}

void StmtPrinter::VisitExtVectorElementExpr(ExtVectorElementExpr *Node) {
  PrintExpr(Node->getBase());
  OS << ".";
  OS << Node->getAccessor().getName();
}

void StmtPrinter::VisitCStyleCastExpr(CStyleCastExpr *Node) {
  OS << '(';
  PrintType(Node->getTypeAsWritten(), OS, Policy);
  OS << ')';
  PrintExpr(Node->getSubExpr());
}

void StmtPrinter::VisitCompoundLiteralExpr(CompoundLiteralExpr *Node) {
  OS << '(';
  PrintType(Node->getType(), OS, Policy);
  OS << ')';
  PrintExpr(Node->getInitializer());
}

void StmtPrinter::VisitImplicitCastExpr(ImplicitCastExpr *Node) {
  // No need to print anything, simply forward to the subexpression.
  PrintExpr(Node->getSubExpr());
}

void StmtPrinter::VisitBinaryOperator(BinaryOperator *Node) {
  PrintExpr(Node->getLHS());
  OS << " " << BinaryOperator::getOpcodeStr(Node->getOpcode()) << " ";
  PrintExpr(Node->getRHS());
}

void StmtPrinter::VisitCompoundAssignOperator(CompoundAssignOperator *Node) {
  PrintExpr(Node->getLHS());
  OS << " " << BinaryOperator::getOpcodeStr(Node->getOpcode()) << " ";
  PrintExpr(Node->getRHS());
}

void StmtPrinter::VisitConditionalOperator(ConditionalOperator *Node) {
  PrintExpr(Node->getCond());
  OS << " ? ";
  PrintExpr(Node->getLHS());
  OS << " : ";
  PrintExpr(Node->getRHS());
}

// GNU extensions.

void StmtPrinter::VisitBinaryConditionalOperator(
    BinaryConditionalOperator *Node) {
  PrintExpr(Node->getCommon());
  OS << " ?: ";
  PrintExpr(Node->getFalseExpr());
}

void StmtPrinter::VisitAddrLabelExpr(AddrLabelExpr *Node) {
  OS << "&amp;&amp;" << Node->getLabel()->getName();
}

void StmtPrinter::VisitStmtExpr(StmtExpr *E) {
  OS << "(";
  PrintRawCompoundStmt(E->getSubStmt());
  OS << ")";
}

void StmtPrinter::VisitChooseExpr(ChooseExpr *Node) {
  OS << "<span class=\"clang keyword\">__builtin_choose_expr</span>(";
  PrintExpr(Node->getCond());
  OS << ", ";
  PrintExpr(Node->getLHS());
  OS << ", ";
  PrintExpr(Node->getRHS());
  OS << ")";
}

void StmtPrinter::VisitGNUNullExpr(GNUNullExpr *) { OS << "__null"; }

void StmtPrinter::VisitShuffleVectorExpr(ShuffleVectorExpr *Node) {
  OS << "__builtin_shufflevector(";
  for (unsigned i = 0, e = Node->getNumSubExprs(); i != e; ++i) {
    if (i) OS << ", ";
    PrintExpr(Node->getExpr(i));
  }
  OS << ")";
}

void StmtPrinter::VisitConvertVectorExpr(ConvertVectorExpr *Node) {
  OS << "__builtin_convertvector(";
  PrintExpr(Node->getSrcExpr());
  OS << ", ";
  PrintType(Node->getType(), OS, Policy);
  OS << ")";
}

void StmtPrinter::VisitInitListExpr(InitListExpr *Node) {
  if (Node->getSyntacticForm()) {
    Visit(Node->getSyntacticForm());
    return;
  }

  OS << "{";
  for (unsigned i = 0, e = Node->getNumInits(); i != e; ++i) {
    if (i) OS << ", ";
    if (Node->getInit(i))
      PrintExpr(Node->getInit(i));
    else
      OS << "{}";
  }
  OS << "}";
}

void StmtPrinter::VisitArrayInitLoopExpr(ArrayInitLoopExpr *Node) {
  // There's no way to express this expression in any of our supported
  // languages, so just emit something terse and (hopefully) clear.
  OS << "{";
  PrintExpr(Node->getSubExpr());
  OS << "}";
}

void StmtPrinter::VisitArrayInitIndexExpr(ArrayInitIndexExpr *Node) {
  OS << "*";
}

void StmtPrinter::VisitParenListExpr(ParenListExpr *Node) {
  OS << "(";
  for (unsigned i = 0, e = Node->getNumExprs(); i != e; ++i) {
    if (i) OS << ", ";
    PrintExpr(Node->getExpr(i));
  }
  OS << ")";
}

void StmtPrinter::VisitDesignatedInitExpr(DesignatedInitExpr *Node) {
  bool NeedsEquals = true;
  for (const DesignatedInitExpr::Designator &D : Node->designators()) {
    if (D.isFieldDesignator()) {
      if (D.getDotLoc().isInvalid()) {
        if (IdentifierInfo *II = D.getFieldName()) {
          OS << II->getName() << ":";
          NeedsEquals = false;
        }
      } else {
        OS << "." << D.getFieldName()->getName();
      }
    } else {
      OS << "[";
      if (D.isArrayDesignator()) {
        PrintExpr(Node->getArrayIndex(D));
      } else {
        PrintExpr(Node->getArrayRangeStart(D));
        OS << " ... ";
        PrintExpr(Node->getArrayRangeEnd(D));
      }
      OS << "]";
    }
  }

  if (NeedsEquals)
    OS << " = ";
  else
    OS << " ";
  PrintExpr(Node->getInit());
}

void StmtPrinter::VisitDesignatedInitUpdateExpr(
    DesignatedInitUpdateExpr *Node) {
  OS << "{";
  OS << "/*base*/";
  PrintExpr(Node->getBase());
  OS << ", ";

  OS << "/*updater*/";
  PrintExpr(Node->getUpdater());
  OS << "}";
}

void StmtPrinter::VisitNoInitExpr(NoInitExpr *Node) { OS << "/*no init*/"; }

void StmtPrinter::VisitImplicitValueInitExpr(ImplicitValueInitExpr *Node) {
  if (Node->getType()->getAsCXXRecordDecl()) {
    OS << "/*implicit*/";
    PrintType(Node->getType(), OS, Policy);
    OS << "()";
  } else {
    OS << "/*implicit*/(";
    PrintType(Node->getType(), OS, Policy);
    OS << ')';
    if (Node->getType()->isRecordType())
      OS << "{}";
    else
      OS << 0;
  }
}

void StmtPrinter::VisitVAArgExpr(VAArgExpr *Node) {
  OS << "__builtin_va_arg(";
  PrintExpr(Node->getSubExpr());
  OS << ", ";
  PrintType(Node->getType(), OS, Policy);
  OS << ")";
}

void StmtPrinter::VisitPseudoObjectExpr(PseudoObjectExpr *Node) {
  PrintExpr(Node->getSyntacticForm());
}

void StmtPrinter::VisitAtomicExpr(AtomicExpr *Node) {
  const char *Name = nullptr;
  switch (Node->getOp()) {
#define BUILTIN(ID, TYPE, ATTRS)
#define ATOMIC_BUILTIN(ID, TYPE, ATTRS) \
  case AtomicExpr::AO##ID:              \
    Name = #ID "(";                     \
    break;
#include <clang/Basic/Builtins.def>
  }
  OS << Name;

  // AtomicExpr stores its subexpressions in a permuted order.
  PrintExpr(Node->getPtr());
  if (Node->getOp() != AtomicExpr::AO__c11_atomic_load &&
      Node->getOp() != AtomicExpr::AO__atomic_load_n &&
      Node->getOp() != AtomicExpr::AO__opencl_atomic_load) {
    OS << ", ";
    PrintExpr(Node->getVal1());
  }
  if (Node->getOp() == AtomicExpr::AO__atomic_exchange || Node->isCmpXChg()) {
    OS << ", ";
    PrintExpr(Node->getVal2());
  }
  if (Node->getOp() == AtomicExpr::AO__atomic_compare_exchange ||
      Node->getOp() == AtomicExpr::AO__atomic_compare_exchange_n) {
    OS << ", ";
    PrintExpr(Node->getWeak());
  }
  if (Node->getOp() != AtomicExpr::AO__c11_atomic_init &&
      Node->getOp() != AtomicExpr::AO__opencl_atomic_init) {
    OS << ", ";
    PrintExpr(Node->getOrder());
  }
  if (Node->isCmpXChg()) {
    OS << ", ";
    PrintExpr(Node->getOrderFail());
  }
  OS << ")";
}

// C++
void StmtPrinter::VisitCXXOperatorCallExpr(CXXOperatorCallExpr *Node) {
  OverloadedOperatorKind Kind = Node->getOperator();
  if (Kind == OO_PlusPlus || Kind == OO_MinusMinus) {
    if (Node->getNumArgs() == 1) {
      OS << getOperatorSpelling(Kind) << ' ';
      PrintExpr(Node->getArg(0));
    } else {
      PrintExpr(Node->getArg(0));
      OS << ' ' << getOperatorSpelling(Kind);
    }
  } else if (Kind == OO_Arrow) {
    PrintExpr(Node->getArg(0));
  } else if (Kind == OO_Call) {
    PrintExpr(Node->getArg(0));
    OS << '(';
    for (unsigned ArgIdx = 1; ArgIdx < Node->getNumArgs(); ++ArgIdx) {
      if (ArgIdx > 1) OS << ", ";
      if (!isa<CXXDefaultArgExpr>(Node->getArg(ArgIdx)))
        PrintExpr(Node->getArg(ArgIdx));
    }
    OS << ')';
  } else if (Kind == OO_Subscript) {
    PrintExpr(Node->getArg(0));
    OS << '[';
    PrintExpr(Node->getArg(1));
    OS << ']';
  } else if (Node->getNumArgs() == 1) {
    OS << getOperatorSpelling(Kind) << ' ';
    PrintExpr(Node->getArg(0));
  } else if (Node->getNumArgs() == 2) {
    PrintExpr(Node->getArg(0));
    OS << ' ' << getOperatorSpelling(Kind) << ' ';
    PrintExpr(Node->getArg(1));
  } else {
    llvm_unreachable("unknown overloaded operator");
  }
}

void StmtPrinter::VisitCXXMemberCallExpr(CXXMemberCallExpr *Node) {
  // If we have a conversion operator call only print the argument.
  CXXMethodDecl *MD = Node->getMethodDecl();
  if (MD && isa<CXXConversionDecl>(MD)) {
    PrintExpr(Node->getImplicitObjectArgument());
    return;
  }
  VisitCallExpr(cast<CallExpr>(Node));
}

void StmtPrinter::VisitCUDAKernelCallExpr(CUDAKernelCallExpr *Node) {
  PrintExpr(Node->getCallee());
  OS << "&lt;&lt;&lt;";
  PrintCallArgs(Node->getConfig());
  OS << "&gt;&gt;&gt;(";
  PrintCallArgs(Node);
  OS << ")";
}

void StmtPrinter::VisitCXXRewrittenBinaryOperator(
    CXXRewrittenBinaryOperator *Node) {
  CXXRewrittenBinaryOperator::DecomposedForm Decomposed =
      Node->getDecomposedForm();
  PrintExpr(const_cast<Expr *>(Decomposed.LHS));
  OS << ' ' << BinaryOperator::getOpcodeStr(Decomposed.Opcode) << ' ';
  PrintExpr(const_cast<Expr *>(Decomposed.RHS));
}

void StmtPrinter::VisitCXXNamedCastExpr(CXXNamedCastExpr *Node) {
  OS << Node->getCastName() << "&lt;";
  PrintType(Node->getTypeAsWritten(), OS, Policy);
  OS << "&gt;(";
  PrintExpr(Node->getSubExpr());
  OS << ")";
}

void StmtPrinter::VisitCXXStaticCastExpr(CXXStaticCastExpr *Node) {
  VisitCXXNamedCastExpr(Node);
}

void StmtPrinter::VisitCXXDynamicCastExpr(CXXDynamicCastExpr *Node) {
  VisitCXXNamedCastExpr(Node);
}

void StmtPrinter::VisitCXXReinterpretCastExpr(CXXReinterpretCastExpr *Node) {
  VisitCXXNamedCastExpr(Node);
}

void StmtPrinter::VisitCXXConstCastExpr(CXXConstCastExpr *Node) {
  VisitCXXNamedCastExpr(Node);
}

void StmtPrinter::VisitBuiltinBitCastExpr(BuiltinBitCastExpr *Node) {
  OS << "__builtin_bit_cast(";
  PrintType(Node->getTypeInfoAsWritten()->getType(), OS, Policy);
  OS << ", ";
  PrintExpr(Node->getSubExpr());
  OS << ")";
}

void StmtPrinter::VisitCXXAddrspaceCastExpr(CXXAddrspaceCastExpr *Node) {
  VisitCXXNamedCastExpr(Node);
}

void StmtPrinter::VisitCXXTypeidExpr(CXXTypeidExpr *Node) {
  OS << "<span class=\"clang keyword\">typeid</span>(";
  if (Node->isTypeOperand()) {
    PrintType(Node->getTypeOperandSourceInfo()->getType(), OS, Policy);
  } else {
    PrintExpr(Node->getExprOperand());
  }
  OS << ")";
}

void StmtPrinter::VisitCXXUuidofExpr(CXXUuidofExpr *Node) {
  OS << "<span class=\"clang keyword\">__uuidof</span>(";
  if (Node->isTypeOperand()) {
    PrintType(Node->getTypeOperandSourceInfo()->getType(), OS, Policy);
  } else {
    PrintExpr(Node->getExprOperand());
  }
  OS << ")";
}

void StmtPrinter::VisitMSPropertyRefExpr(MSPropertyRefExpr *Node) {
  PrintExpr(Node->getBaseExpr());
  if (Node->isArrow())
    OS << "-&gt;";
  else
    OS << ".";
  if (NestedNameSpecifier *Qualifier =
          Node->getQualifierLoc().getNestedNameSpecifier())
    Qualifier->print(OS, Policy);
  OS << Node->getPropertyDecl()->getDeclName();
}

void StmtPrinter::VisitMSPropertySubscriptExpr(MSPropertySubscriptExpr *Node) {
  PrintExpr(Node->getBase());
  OS << "[";
  PrintExpr(Node->getIdx());
  OS << "]";
}

void StmtPrinter::VisitUserDefinedLiteral(UserDefinedLiteral *Node) {
  switch (Node->getLiteralOperatorKind()) {
    case UserDefinedLiteral::LOK_Raw:
      OS << cast<StringLiteral>(Node->getArg(0)->IgnoreImpCasts())->getString();
      break;
    case UserDefinedLiteral::LOK_Template: {
      const auto *DRE = cast<DeclRefExpr>(Node->getCallee()->IgnoreImpCasts());
      const TemplateArgumentList *Args =
          cast<FunctionDecl>(DRE->getDecl())->getTemplateSpecializationArgs();
      assert(Args);

      if (Args->size() != 1) {
        OS << "<span class=\"clang keyword\">operator</span>\"\""
           << Node->getUDSuffix()->getName();
        printTemplateArgumentList(OS, Args->asArray(), Policy);
        OS << "()";
        return;
      }

      const TemplateArgument &Pack = Args->get(0);
      for (const auto &P : Pack.pack_elements()) {
        char C = (char)P.getAsIntegral().getZExtValue();
        OS << C;
      }
      break;
    }
    case UserDefinedLiteral::LOK_Integer: {
      // Print integer literal without suffix.
      const auto *Int = cast<IntegerLiteral>(Node->getCookedLiteral());
      OS << toString(Int->getValue(), 10, /*isSigned*/ false);
      break;
    }
    case UserDefinedLiteral::LOK_Floating: {
      // Print floating literal without suffix.
      auto *Float = cast<FloatingLiteral>(Node->getCookedLiteral());
      PrintFloatingLiteral(OS, Float, /*PrintSuffix=*/false);
      break;
    }
    case UserDefinedLiteral::LOK_String:
    case UserDefinedLiteral::LOK_Character:
      PrintExpr(Node->getCookedLiteral());
      break;
  }
  OS << Node->getUDSuffix()->getName();
}

void StmtPrinter::VisitCXXBoolLiteralExpr(CXXBoolLiteralExpr *Node) {
  OS << "<span class=\"clang keyword boolean\">"
     << (Node->getValue() ? "true" : "false") << "</span>";
}

void StmtPrinter::VisitCXXNullPtrLiteralExpr(CXXNullPtrLiteralExpr *Node) {
  OS << "<span class=\"clang keyword\">nullptr</span>";
}

void StmtPrinter::VisitCXXThisExpr(CXXThisExpr *Node) {
  OS << "<span class=\"clang keyword\">this</span>";
}

void StmtPrinter::VisitCXXThrowExpr(CXXThrowExpr *Node) {
  if (!Node->getSubExpr())
    OS << "<span class=\"clang keyword\">throw</span>";
  else {
    OS << "<span class=\"clang keyword\">throw</span> ";
    PrintExpr(Node->getSubExpr());
  }
}

void StmtPrinter::VisitCXXDefaultArgExpr(CXXDefaultArgExpr *Node) {
  // Nothing to print: we picked up the default argument.
}

void StmtPrinter::VisitCXXDefaultInitExpr(CXXDefaultInitExpr *Node) {
  // Nothing to print: we picked up the default initializer.
}

void StmtPrinter::VisitCXXFunctionalCastExpr(CXXFunctionalCastExpr *Node) {
  PrintType(Node->getType(), OS, Policy);
  // If there are no parens, this is list-initialization, and the braces are
  // part of the syntax of the inner construct.
  if (Node->getLParenLoc().isValid()) OS << "(";
  PrintExpr(Node->getSubExpr());
  if (Node->getLParenLoc().isValid()) OS << ")";
}

void StmtPrinter::VisitCXXBindTemporaryExpr(CXXBindTemporaryExpr *Node) {
  PrintExpr(Node->getSubExpr());
}

void StmtPrinter::VisitCXXTemporaryObjectExpr(CXXTemporaryObjectExpr *Node) {
  PrintType(Node->getType(), OS, Policy);
  if (Node->isStdInitListInitialization())
    /* Nothing to do; braces are part of creating the std::initializer_list. */;
  else if (Node->isListInitialization())
    OS << "{";
  else
    OS << "(";
  for (CXXTemporaryObjectExpr::arg_iterator Arg = Node->arg_begin(),
                                            ArgEnd = Node->arg_end();
       Arg != ArgEnd; ++Arg) {
    if ((*Arg)->isDefaultArgument()) break;
    if (Arg != Node->arg_begin()) OS << ", ";
    PrintExpr(*Arg);
  }
  if (Node->isStdInitListInitialization())
    /* See above. */;
  else if (Node->isListInitialization())
    OS << "}";
  else
    OS << ")";
}

void StmtPrinter::VisitLambdaExpr(LambdaExpr *Node) {
  OS << '[';
  bool NeedComma = false;
  switch (Node->getCaptureDefault()) {
    case LCD_None:
      break;

    case LCD_ByCopy:
      OS << '=';
      NeedComma = true;
      break;

    case LCD_ByRef:
      OS << "&amp;";
      NeedComma = true;
      break;
  }
  for (LambdaExpr::capture_iterator C = Node->explicit_capture_begin(),
                                    CEnd = Node->explicit_capture_end();
       C != CEnd; ++C) {
    if (C->capturesVLAType()) continue;

    if (NeedComma) OS << ", ";
    NeedComma = true;

    switch (C->getCaptureKind()) {
      case LCK_This:
        OS << "<span class=\"clang keyword\">this</span>";
        break;

      case LCK_StarThis:
        OS << "*<span class=\"clang keyword\">this</span>";
        break;

      case LCK_ByRef:
        if (Node->getCaptureDefault() != LCD_ByRef || Node->isInitCapture(C))
          OS << "&amp;";
        OS << C->getCapturedVar()->getName();
        break;

      case LCK_ByCopy:
        OS << C->getCapturedVar()->getName();
        break;

      case LCK_VLAType:
        llvm_unreachable("VLA type in explicit captures.");
    }

    if (C->isPackExpansion()) OS << "...";

    if (Node->isInitCapture(C)) {
      VarDecl *D = C->getCapturedVar();

      llvm::StringRef Pre;
      llvm::StringRef Post;
      if (D->getInitStyle() == VarDecl::CallInit &&
          !isa<ParenListExpr>(D->getInit())) {
        Pre = "(";
        Post = ")";
      } else if (D->getInitStyle() == VarDecl::CInit) {
        Pre = " = ";
      }

      OS << Pre;
      PrintExpr(D->getInit());
      OS << Post;
    }
  }
  OS << ']';

  if (!Node->getExplicitTemplateParameters().empty()) {
    Node->getTemplateParameterList()->print(
        OS, Node->getLambdaClass()->getASTContext(),
        /*OmitTemplateKW*/ true);
  }

  if (Node->hasExplicitParameters()) {
    OS << '(';
    CXXMethodDecl *Method = Node->getCallOperator();
    NeedComma = false;
    for (const auto *P : Method->parameters()) {
      if (NeedComma) {
        OS << ", ";
      } else {
        NeedComma = true;
      }
      std::string ParamStr = P->getNameAsString();
      PrintType(P->getOriginalType(), OS, Policy, ParamStr);
    }
    if (Method->isVariadic()) {
      if (NeedComma) OS << ", ";
      OS << "...";
    }
    OS << ')';

    if (Node->isMutable())
      OS << " <span class=\"clang keyword\">mutable</span>";

    auto *Proto = Method->getType()->castAs<FunctionProtoType>();
    Proto->printExceptionSpecification(OS, Policy);

    // FIXME: Attributes

    // Print the trailing return type if it was specified in the source.
    if (Node->hasExplicitResultType()) {
      OS << " -&gt; ";
      PrintType(Proto->getReturnType(), OS, Policy);
    }
  }

  // Print the body.
  OS << ' ';
  if (Policy.TerseOutput)
    OS << "{}";
  else
    PrintRawCompoundStmt(Node->getCompoundStmtBody());
}

void StmtPrinter::VisitCXXScalarValueInitExpr(CXXScalarValueInitExpr *Node) {
  if (TypeSourceInfo *TSInfo = Node->getTypeSourceInfo())
    PrintType(TSInfo->getType(), OS, Policy);
  else
    PrintType(Node->getType(), OS, Policy);
  OS << "()";
}

void StmtPrinter::VisitCXXNewExpr(CXXNewExpr *E) {
  if (E->isGlobalNew()) OS << "::";
  OS << "<span class=\"clang keyword\">new</span> ";
  unsigned NumPlace = E->getNumPlacementArgs();
  if (NumPlace > 0 && !isa<CXXDefaultArgExpr>(E->getPlacementArg(0))) {
    OS << "(";
    PrintExpr(E->getPlacementArg(0));
    for (unsigned i = 1; i < NumPlace; ++i) {
      if (isa<CXXDefaultArgExpr>(E->getPlacementArg(i))) break;
      OS << ", ";
      PrintExpr(E->getPlacementArg(i));
    }
    OS << ") ";
  }
  if (E->isParenTypeId()) OS << "(";
  std::string TypeS;
  if (Optional<Expr *> Size = E->getArraySize()) {
    llvm::raw_string_ostream s(TypeS);
    s << '[';
    if (*Size) ::PrintStmt(*Size, s, Policy, 0, nullptr, Helper);
    s << ']';
  }
  PrintType(E->getAllocatedType(), OS, Policy, TypeS);
  if (E->isParenTypeId()) OS << ")";

  CXXNewExpr::InitializationStyle InitStyle = E->getInitializationStyle();
  if (InitStyle) {
    if (InitStyle == CXXNewExpr::CallInit) OS << "(";
    PrintExpr(E->getInitializer());
    if (InitStyle == CXXNewExpr::CallInit) OS << ")";
  }
}

void StmtPrinter::VisitCXXDeleteExpr(CXXDeleteExpr *E) {
  if (E->isGlobalDelete()) OS << "::";
  OS << "<span class=\"clang keyword\">delete</span> ";
  if (E->isArrayForm()) OS << "[] ";
  PrintExpr(E->getArgument());
}

void StmtPrinter::VisitCXXPseudoDestructorExpr(CXXPseudoDestructorExpr *E) {
  PrintExpr(E->getBase());
  if (E->isArrow())
    OS << "-&gt;";
  else
    OS << '.';
  if (E->getQualifier()) E->getQualifier()->print(OS, Policy);
  OS << "~";

  if (IdentifierInfo *II = E->getDestroyedTypeIdentifier())
    OS << II->getName();
  else
    PrintType(E->getDestroyedType(), OS, Policy);
}

void StmtPrinter::VisitCXXConstructExpr(CXXConstructExpr *E) {
  if (E->isListInitialization() && !E->isStdInitListInitialization()) OS << "{";

  for (unsigned i = 0, e = E->getNumArgs(); i != e; ++i) {
    if (isa<CXXDefaultArgExpr>(E->getArg(i))) {
      // Don't print any defaulted arguments
      break;
    }

    if (i) OS << ", ";
    PrintExpr(E->getArg(i));
  }

  if (E->isListInitialization() && !E->isStdInitListInitialization()) OS << "}";
}

void StmtPrinter::VisitCXXInheritedCtorInitExpr(CXXInheritedCtorInitExpr *E) {
  // Parens are printed by the surrounding context.
  OS << "&lt;forwarded&gt;";
}

void StmtPrinter::VisitCXXStdInitializerListExpr(CXXStdInitializerListExpr *E) {
  PrintExpr(E->getSubExpr());
}

void StmtPrinter::VisitExprWithCleanups(ExprWithCleanups *E) {
  // Just forward to the subexpression.
  PrintExpr(E->getSubExpr());
}

void StmtPrinter::VisitCXXUnresolvedConstructExpr(
    CXXUnresolvedConstructExpr *Node) {
  PrintType(Node->getTypeAsWritten(), OS, Policy);
  OS << "(";
  for (CXXUnresolvedConstructExpr::arg_iterator Arg = Node->arg_begin(),
                                                ArgEnd = Node->arg_end();
       Arg != ArgEnd; ++Arg) {
    if (Arg != Node->arg_begin()) OS << ", ";
    PrintExpr(*Arg);
  }
  OS << ")";
}

void StmtPrinter::VisitCXXDependentScopeMemberExpr(
    CXXDependentScopeMemberExpr *Node) {
  if (!Node->isImplicitAccess()) {
    PrintExpr(Node->getBase());
    OS << (Node->isArrow() ? "-&gt;" : ".");
  }
  if (NestedNameSpecifier *Qualifier = Node->getQualifier())
    Qualifier->print(OS, Policy);
  if (Node->hasTemplateKeyword())
    OS << "<span class=\"clang keyword\">template</span> ";
  OS << Node->getMemberNameInfo();
  if (Node->hasExplicitTemplateArgs())
    printTemplateArgumentList(OS, Node->template_arguments(), Policy);
}

void StmtPrinter::VisitUnresolvedMemberExpr(UnresolvedMemberExpr *Node) {
  if (!Node->isImplicitAccess()) {
    PrintExpr(Node->getBase());
    OS << (Node->isArrow() ? "-&gt;" : ".");
  }
  if (NestedNameSpecifier *Qualifier = Node->getQualifier())
    Qualifier->print(OS, Policy);
  if (Node->hasTemplateKeyword())
    OS << "<span class=\"clang keyword\">template</span> ";
  OS << Node->getMemberNameInfo();
  if (Node->hasExplicitTemplateArgs())
    printTemplateArgumentList(OS, Node->template_arguments(), Policy);
}

void StmtPrinter::VisitTypeTraitExpr(TypeTraitExpr *E) {
  OS << getTraitSpelling(E->getTrait()) << "(";
  for (unsigned I = 0, N = E->getNumArgs(); I != N; ++I) {
    if (I > 0) OS << ", ";
    PrintType(E->getArg(I)->getType(), OS, Policy);
  }
  OS << ")";
}

void StmtPrinter::VisitArrayTypeTraitExpr(ArrayTypeTraitExpr *E) {
  OS << getTraitSpelling(E->getTrait()) << '(';
  PrintType(E->getQueriedType(), OS, Policy);
  OS << ')';
}

void StmtPrinter::VisitExpressionTraitExpr(ExpressionTraitExpr *E) {
  OS << getTraitSpelling(E->getTrait()) << '(';
  PrintExpr(E->getQueriedExpression());
  OS << ')';
}

void StmtPrinter::VisitCXXNoexceptExpr(CXXNoexceptExpr *E) {
  OS << "<span class=\"clang keyword\">noexcept</span>(";
  PrintExpr(E->getOperand());
  OS << ")";
}

void StmtPrinter::VisitPackExpansionExpr(PackExpansionExpr *E) {
  PrintExpr(E->getPattern());
  OS << "...";
}

void StmtPrinter::VisitSizeOfPackExpr(SizeOfPackExpr *E) {
  OS << "<span class=\"clang keyword\">sizeof</span>...(" << *E->getPack()
     << ")";
}

void StmtPrinter::VisitSubstNonTypeTemplateParmPackExpr(
    SubstNonTypeTemplateParmPackExpr *Node) {
  OS << *Node->getParameterPack();
}

void StmtPrinter::VisitSubstNonTypeTemplateParmExpr(
    SubstNonTypeTemplateParmExpr *Node) {
  Visit(Node->getReplacement());
}

void StmtPrinter::VisitFunctionParmPackExpr(FunctionParmPackExpr *E) {
  OS << *E->getParameterPack();
}

void StmtPrinter::VisitMaterializeTemporaryExpr(
    MaterializeTemporaryExpr *Node) {
  PrintExpr(Node->getSubExpr());
}

void StmtPrinter::VisitCXXFoldExpr(CXXFoldExpr *E) {
  OS << "(";
  if (E->getLHS()) {
    PrintExpr(E->getLHS());
    OS << " " << BinaryOperator::getOpcodeStr(E->getOperator()) << " ";
  }
  OS << "...";
  if (E->getRHS()) {
    OS << " " << BinaryOperator::getOpcodeStr(E->getOperator()) << " ";
    PrintExpr(E->getRHS());
  }
  OS << ")";
}

void StmtPrinter::VisitConceptSpecializationExpr(ConceptSpecializationExpr *E) {
  NestedNameSpecifierLoc NNS = E->getNestedNameSpecifierLoc();
  if (NNS) NNS.getNestedNameSpecifier()->print(OS, Policy);
  if (E->getTemplateKWLoc().isValid())
    OS << "<span class=\"clang keyword\">template</span> ";
  OS << E->getFoundDecl()->getName();
  printTemplateArgumentList(OS, E->getTemplateArgsAsWritten()->arguments(),
                            Policy);
}

void StmtPrinter::VisitRequiresExpr(RequiresExpr *E) {
  OS << "<span class=\"clang keyword\">requires</span> ";
  auto LocalParameters = E->getLocalParameters();
  if (!LocalParameters.empty()) {
    OS << "(";
    for (ParmVarDecl *LocalParam : LocalParameters) {
      PrintRawDecl(LocalParam);
      if (LocalParam != LocalParameters.back()) OS << ", ";
    }

    OS << ") ";
  }
  OS << "{ ";
  auto Requirements = E->getRequirements();
  for (concepts::Requirement *Req : Requirements) {
    if (auto *TypeReq = dyn_cast<concepts::TypeRequirement>(Req)) {
      if (TypeReq->isSubstitutionFailure())
        OS << "&lt;&lt;error-type&gt;&gt;";
      else
        PrintType(TypeReq->getType()->getType(), OS, Policy);
    } else if (auto *ExprReq = dyn_cast<concepts::ExprRequirement>(Req)) {
      if (ExprReq->isCompound()) OS << "{ ";
      if (ExprReq->isExprSubstitutionFailure())
        OS << "&lt;&lt;error-expression&gt;&gt;";
      else
        PrintExpr(ExprReq->getExpr());
      if (ExprReq->isCompound()) {
        OS << " }";
        if (ExprReq->getNoexceptLoc().isValid())
          OS << " <span class=\"clang keyword\">noexcept</span>";
        const auto &RetReq = ExprReq->getReturnTypeRequirement();
        if (!RetReq.isEmpty()) {
          OS << " -&gt; ";
          if (RetReq.isSubstitutionFailure())
            OS << "&lt;&lt;error-type&gt;&gt;";
          else if (RetReq.isTypeConstraint())
            RetReq.getTypeConstraint()->print(OS, Policy);
        }
      }
    } else {
      auto *NestedReq = cast<concepts::NestedRequirement>(Req);
      OS << "<span class=\"clang keyword\">requires</span> ";
      if (NestedReq->isSubstitutionFailure())
        OS << "&lt;&lt;error-expression&gt;&gt;";
      else
        PrintExpr(NestedReq->getConstraintExpr());
    }
    OS << "; ";
  }
  OS << "}";
}

// C++ Coroutines TS

void StmtPrinter::VisitCoroutineBodyStmt(CoroutineBodyStmt *S) {
  Visit(S->getBody());
}

void StmtPrinter::VisitCoreturnStmt(CoreturnStmt *S) {
  OS << "<span class=\"clang keyword\">co_return</span>";
  if (S->getOperand()) {
    OS << " ";
    Visit(S->getOperand());
  }
  OS << ";";
}

void StmtPrinter::VisitCoawaitExpr(CoawaitExpr *S) {
  OS << "<span class=\"clang keyword\">co_await</span> ";
  PrintExpr(S->getOperand());
}

void StmtPrinter::VisitDependentCoawaitExpr(DependentCoawaitExpr *S) {
  OS << "<span class=\"clang keyword\">co_await</span> ";
  PrintExpr(S->getOperand());
}

void StmtPrinter::VisitCoyieldExpr(CoyieldExpr *S) {
  OS << "<span class=\"clang keyword\">co_yield</span> ";
  PrintExpr(S->getOperand());
}

// Obj-C

void StmtPrinter::VisitObjCStringLiteral(ObjCStringLiteral *Node) {
  OS << "@";
  VisitStringLiteral(Node->getString());
}

void StmtPrinter::VisitObjCBoxedExpr(ObjCBoxedExpr *E) {
  OS << "@";
  Visit(E->getSubExpr());
}

void StmtPrinter::VisitObjCArrayLiteral(ObjCArrayLiteral *E) {
  OS << "@[ ";
  ObjCArrayLiteral::child_range Ch = E->children();
  for (auto I = Ch.begin(), E = Ch.end(); I != E; ++I) {
    if (I != Ch.begin()) OS << ", ";
    Visit(*I);
  }
  OS << " ]";
}

void StmtPrinter::VisitObjCDictionaryLiteral(ObjCDictionaryLiteral *E) {
  OS << "@{ ";
  for (unsigned I = 0, N = E->getNumElements(); I != N; ++I) {
    if (I > 0) OS << ", ";

    ObjCDictionaryElement Element = E->getKeyValueElement(I);
    Visit(Element.Key);
    OS << " : ";
    Visit(Element.Value);
    if (Element.isPackExpansion()) OS << "...";
  }
  OS << " }";
}

void StmtPrinter::VisitObjCEncodeExpr(ObjCEncodeExpr *Node) {
  OS << "<span class=\"clang keyword objective-c\">@encode</span>(";
  PrintType(Node->getEncodedType(), OS, Policy);
  OS << ')';
}

void StmtPrinter::VisitObjCSelectorExpr(ObjCSelectorExpr *Node) {
  OS << "<span class=\"clang keyword objective-c\">@selector</span>(";
  Node->getSelector().print(OS);
  OS << ')';
}

void StmtPrinter::VisitObjCProtocolExpr(ObjCProtocolExpr *Node) {
  OS << "<span class=\"clang keyword objective-c\">@protocol</span>("
     << *Node->getProtocol() << ')';
}

void StmtPrinter::VisitObjCMessageExpr(ObjCMessageExpr *Mess) {
  OS << "[";
  switch (Mess->getReceiverKind()) {
    case ObjCMessageExpr::Instance:
      PrintExpr(Mess->getInstanceReceiver());
      break;

    case ObjCMessageExpr::Class:
      PrintType(Mess->getClassReceiver(), OS, Policy);
      break;

    case ObjCMessageExpr::SuperInstance:
    case ObjCMessageExpr::SuperClass:
      OS << "<span class=\"clang keyword\">Super</span>";
      break;
  }

  OS << ' ';
  Selector selector = Mess->getSelector();
  if (selector.isUnarySelector()) {
    OS << selector.getNameForSlot(0);
  } else {
    for (unsigned i = 0, e = Mess->getNumArgs(); i != e; ++i) {
      if (i < selector.getNumArgs()) {
        if (i > 0) OS << ' ';
        if (selector.getIdentifierInfoForSlot(i))
          OS << selector.getIdentifierInfoForSlot(i)->getName() << ':';
        else
          OS << ":";
      } else
        OS << ", ";  // Handle variadic methods.

      PrintExpr(Mess->getArg(i));
    }
  }
  OS << "]";
}

void StmtPrinter::VisitObjCBoolLiteralExpr(ObjCBoolLiteralExpr *Node) {
  OS << "<span class=\"clang keyword\">"
     << (Node->getValue() ? "__objc_yes" : "__objc_no") << "</span>";
}

void StmtPrinter::VisitObjCIndirectCopyRestoreExpr(
    ObjCIndirectCopyRestoreExpr *E) {
  PrintExpr(E->getSubExpr());
}

void StmtPrinter::VisitObjCBridgedCastExpr(ObjCBridgedCastExpr *E) {
  OS << '(' << E->getBridgeKindName();
  PrintType(E->getType(), OS, Policy);
  OS << ')';
  PrintExpr(E->getSubExpr());
}

void StmtPrinter::VisitBlockExpr(BlockExpr *Node) {
  BlockDecl *BD = Node->getBlockDecl();
  OS << "^";

  const FunctionType *AFT = Node->getFunctionType();

  if (isa<FunctionNoProtoType>(AFT)) {
    OS << "()";
  } else if (!BD->param_empty() || cast<FunctionProtoType>(AFT)->isVariadic()) {
    OS << '(';
    for (BlockDecl::param_iterator AI = BD->param_begin(), E = BD->param_end();
         AI != E; ++AI) {
      if (AI != BD->param_begin()) OS << ", ";
      std::string ParamStr = (*AI)->getNameAsString();
      PrintType((*AI)->getType(), OS, Policy, ParamStr);
    }

    const auto *FT = cast<FunctionProtoType>(AFT);
    if (FT->isVariadic()) {
      if (!BD->param_empty()) OS << ", ";
      OS << "...";
    }
    OS << ')';
  }
  OS << "{ }";
}

void StmtPrinter::VisitOpaqueValueExpr(OpaqueValueExpr *Node) {
  PrintExpr(Node->getSourceExpr());
}

void StmtPrinter::VisitTypoExpr(TypoExpr *Node) {
  // TODO: Print something reasonable for a TypoExpr, if necessary.
  llvm_unreachable("Cannot print TypoExpr nodes");
}

void StmtPrinter::VisitRecoveryExpr(RecoveryExpr *Node) {
  OS << "&lt;recovery-expr&gt;(";
  const char *Sep = "";
  for (Expr *E : Node->subExpressions()) {
    OS << Sep;
    PrintExpr(E);
    Sep = ", ";
  }
  OS << ')';
}

void StmtPrinter::VisitAsTypeExpr(AsTypeExpr *Node) {
  OS << "__builtin_astype(";
  PrintExpr(Node->getSrcExpr());
  OS << ", ";
  PrintType(Node->getType(), OS, Policy);
  OS << ")";
}

//===----------------------------------------------------------------------===//
// Stmt method implementations
//===----------------------------------------------------------------------===//

void PrintStmt(clang::Stmt *stmt, llvm::raw_ostream &Out,
               const clang::PrintingPolicy &Policy, int Indentation,
               const clang::ASTContext *Context, clang::PrinterHelper *Helper) {
  StmtPrinter P(Out, Helper, Policy, Indentation, "\n", Context);
  P.Visit(stmt);
}
