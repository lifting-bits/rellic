/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/Decl.h>
#include <clang/AST/DeclVisitor.h>
#include <clang/AST/Expr.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/StmtVisitor.h>
#include <clang/AST/Type.h>
#include <clang/AST/TypeVisitor.h>
#include <clang/Frontend/ASTUnit.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

namespace rellic {

struct Token {
  union ASTNodeRef {
    clang::Stmt *stmt;
    clang::Decl *decl;
    clang::QualType type;
  } node;

  std::string str;
};

class DeclTokenizer : public clang::DeclVisitor<DeclTokenizer> {
 private:
  std::list<Token> &out;
  const clang::ASTUnit &unit;

  void PrintAttributes(clang::Decl *decl);
  void PrintDeclType(clang::QualType type, llvm::StringRef decl_name);
  void PrintPragmas(clang::Decl *decl);
  void ProcessDeclGroup(llvm::SmallVectorImpl<clang::Decl *> &decls);

 public:
  DeclTokenizer(std::list<Token> &out, const clang::ASTUnit &unit)
      : out(out), unit(unit) {}

  void PrintGroup(clang::Decl **begin, unsigned num_decls);

  void VisitDecl(clang::Decl *dec) {
    LOG(FATAL) << "Unimplemented decl handler!";
  }

  void VisitVarDecl(clang::VarDecl *decl);
  void VisitParmVarDecl(clang::ParmVarDecl *decl);
  void VisitDeclContext(clang::DeclContext *dctx);
  void VisitFunctionDecl(clang::FunctionDecl *decl);
  void VisitTranslationUnitDecl(clang::TranslationUnitDecl *decl);
};

class StmtTokenizer : public clang::StmtVisitor<StmtTokenizer> {
 private:
  std::list<Token> &out;
  const clang::ASTUnit &unit;

  void PrintStmt(clang::Stmt *stmt);
  void PrintExpr(clang::Expr *expr);

 public:
  StmtTokenizer(std::list<Token> &out, const clang::ASTUnit &unit)
      : out(out), unit(unit) {}

  void VisitStmt(clang::Stmt *stmt) {
    LOG(FATAL) << "Unimplemented stmt handler!";
  }

  void VisitCompoundStmt(clang::CompoundStmt *stmt);
  void VisitDeclStmt(clang::DeclStmt *stmt);
  void VisitIfStmt(clang::IfStmt *stmt);
  void VisitReturnStmt(clang::ReturnStmt *stmt);

  void VisitIntegerLiteral(clang::IntegerLiteral *ilit);
  void VisitDeclRefExpr(clang::DeclRefExpr *ref);
  void VisitParenExpr(clang::ParenExpr *paren);
  void VisitCStyleCastExpr(clang::CStyleCastExpr *cast);
  void VisitImplicitCastExpr(clang::ImplicitCastExpr *cast);
  void VisitArraySubscriptExpr(clang::ArraySubscriptExpr *sub);
  void VisitBinaryOperator(clang::BinaryOperator *binop);
};

}  // namespace rellic