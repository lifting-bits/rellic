/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/ASTDumperUtils.h>
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

#include <variant>

namespace rellic {

enum TokenKind { Stmt, Decl, Type, Space, Newline, Indent, Misc };

class Token {
 private:
  TokenKind kind;
  std::variant<std::monostate, clang::Stmt *, clang::Decl *, clang::QualType>
      node;
  std::string str;

  Token(TokenKind kind) : kind(kind) {}
  Token(std::string &str) : kind(TokenKind::Misc), str(str) {}

  template <typename T>
  Token(TokenKind kind, T node, std::string &str)
      : kind(kind), node(node), str(str) {}

 public:
  static Token CreateStmt(clang::Stmt *stmt, std::string str) {
    return Token(TokenKind::Stmt, stmt, str);
  }

  static Token CreateDecl(clang::Decl *decl, std::string str) {
    return Token(TokenKind::Decl, decl, str);
  }

  static Token CreateType(clang::QualType type, std::string str) {
    return Token(TokenKind::Type, type, str);
  }

  static Token CreateSpace() { return Token(TokenKind::Space); }
  static Token CreateNewline() { return Token(TokenKind::Newline); }
  static Token CreateIndent() { return Token(TokenKind::Indent); }

  static Token CreateMisc(std::string str) { return Token(str); }

  const std::string &GetString() { return str; }
  TokenKind GetKind() { return kind; }

  clang::Stmt *GetStmt() {
    CHECK(std::holds_alternative<clang::Stmt *>(node));
    return std::get<clang::Stmt *>(node);
  }
};

class DeclTokenizer : public clang::DeclVisitor<DeclTokenizer> {
 private:
  std::list<Token> &out;
  const clang::ASTUnit &unit;

  unsigned indent;

  void Indent();
  void PrintAttributes(clang::Decl *decl);
  void PrintPragmas(clang::Decl *decl);
  void ProcessDeclGroup(llvm::SmallVectorImpl<clang::Decl *> &decls);

 public:
  DeclTokenizer(std::list<Token> &out, const clang::ASTUnit &unit,
                unsigned indent = 0U)
      : out(out), unit(unit), indent(indent) {}

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

  unsigned indent;

  void Indent();
  void PrintStmt(clang::Stmt *stmt);
  void PrintRawInitStmt(clang::Stmt *stmt, unsigned prefix_width);
  void PrintExpr(clang::Expr *expr);
  void PrintRawCompoundStmt(clang::CompoundStmt *stmt);
  void PrintRawDeclStmt(clang::DeclStmt *stmt);
  void PrintRawIfStmt(clang::IfStmt *ifstmt);

 public:
  StmtTokenizer(std::list<Token> &out, const clang::ASTUnit &unit,
                unsigned indent = 0U)
      : out(out), unit(unit), indent(indent) {}

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