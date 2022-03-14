/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/Util.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Expr.h>
#include <clang/AST/ODRHash.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/StmtVisitor.h>
#include <clang/Basic/LangOptions.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/Support/raw_ostream.h>

#include "rellic/AST/ASTBuilder.h"
#include "rellic/Exception.h"

namespace rellic {

unsigned GetHash(clang::Stmt *stmt) {
  llvm::FoldingSetNodeID id;
  clang::ODRHash hash;
  stmt->ProcessODRHash(id, hash);
  return hash.CalculateHash();
}

static bool IsEquivalent(clang::Stmt *a, clang::Stmt *b,
                         llvm::FoldingSetNodeID &foldingSetA,
                         llvm::FoldingSetNodeID &foldingSetB,
                         clang::ODRHash &hashA, clang::ODRHash &hashB) {
  if (a == b) {
    return true;
  }

  if (a->getStmtClass() != b->getStmtClass()) {
    return false;
  }

  foldingSetA.clear();
  foldingSetB.clear();
  hashA.clear();
  hashB.clear();
  a->ProcessODRHash(foldingSetA, hashA);
  b->ProcessODRHash(foldingSetB, hashB);

  if (foldingSetA == foldingSetB) {
    return true;
  }

  auto child_a{a->child_begin()};
  auto child_b{b->child_begin()};
  while (true) {
    bool a_end{child_a == a->child_end()};
    bool b_end{child_b == b->child_end()};
    if (a_end != b_end) {
      return false;
    } else if (a_end && b_end) {
      return true;
    } else if (!IsEquivalent(*child_a, *child_b, foldingSetA, foldingSetB,
                             hashA, hashB)) {
      return false;
    }

    ++child_a;
    ++child_b;
  }
}

bool IsEquivalent(clang::Stmt *a, clang::Stmt *b) {
  llvm::FoldingSetNodeID idA, idB;
  clang::ODRHash hashA, hashB;
  return IsEquivalent(a, b, idA, idB, hashA, hashB);
}

void CopyProvenance(clang::Stmt *from, clang::Stmt *to, StmtToIRMap &map) {
  auto range{map.equal_range(from)};
  std::vector<std::pair<clang::Stmt *, llvm::Value *>> pairs;
  for (auto it{range.first}; it != range.second; ++it) {
    pairs.emplace_back(to, it->second);
  }
  map.insert(pairs.begin(), pairs.end());
}

class ExprCloner : public clang::StmtVisitor<ExprCloner, clang::Expr *> {
  ASTBuilder ast;
  clang::ASTContext &ctx;
  StmtToIRMap &provenance;
  bool shallow;

  clang::Expr *Clone(clang::Expr *e) {
    if (shallow) {
      return e;
    } else {
      return Visit(e);
    }
  }

 public:
  ExprCloner(clang::ASTUnit &unit, StmtToIRMap &provenance,
             bool shallow = false)
      : ast(unit),
        ctx(unit.getASTContext()),
        provenance(provenance),
        shallow(shallow) {}

  clang::Expr *VisitIntegerLiteral(clang::IntegerLiteral *expr) {
    return ast.CreateIntLit(expr->getValue());
  }

  clang::Expr *VisitCharacterLiteral(clang::CharacterLiteral *expr) {
    return ast.CreateCharLit(expr->getValue());
  }

  clang::Expr *VisitStringLiteral(clang::StringLiteral *expr) {
    return ast.CreateStrLit(expr->getString().str());
  }

  clang::Expr *VisitFloatingLiteral(clang::FloatingLiteral *expr) {
    return ast.CreateFPLit(expr->getValue());
  }

  clang::Expr *VisitCastExpr(clang::CastExpr *expr) {
    return ast.CreateCStyleCast(expr->getType(), Clone(expr->getSubExpr()));
  }

  clang::Expr *VisitImplicitCastExpr(clang::ImplicitCastExpr *expr) {
    return Visit(expr->getSubExpr());
  }

  clang::Expr *VisitUnaryOperator(clang::UnaryOperator *expr) {
    return ast.CreateUnaryOp(expr->getOpcode(), Clone(expr->getSubExpr()));
  }

  clang::Expr *VisitBinaryOperator(clang::BinaryOperator *expr) {
    return ast.CreateBinaryOp(expr->getOpcode(), Clone(expr->getLHS()),
                              Clone(expr->getRHS()));
  }

  clang::Expr *VisitConditionalOperator(clang::ConditionalOperator *expr) {
    return ast.CreateConditional(Clone(expr->getCond()),
                                 Clone(expr->getTrueExpr()),
                                 Clone(expr->getFalseExpr()));
  }

  clang::Expr *VisitArraySubscriptExpr(clang::ArraySubscriptExpr *expr) {
    return ast.CreateArraySub(Clone(expr->getBase()), Clone(expr->getIdx()));
  }

  clang::Expr *VisitCallExpr(clang::CallExpr *expr) {
    std::vector<clang::Expr *> args;
    for (auto arg : expr->arguments()) {
      args.push_back(Clone(arg));
    }
    return ast.CreateCall(Clone(expr->getCallee()), args);
  }

  clang::Expr *VisitMemberExpr(clang::MemberExpr *expr) {
    return clang::MemberExpr::Create(
        ctx, expr->getBase(), expr->isArrow(), clang::SourceLocation(),
        expr->getQualifierLoc(), clang::SourceLocation(), expr->getMemberDecl(),
        expr->getFoundDecl(), expr->getMemberNameInfo(),
        /*FIXME(frabert)*/ nullptr, expr->getType(), expr->getValueKind(),
        expr->getObjectKind(), expr->isNonOdrUse());
  }

  clang::Expr *VisitDeclRefExpr(clang::DeclRefExpr *expr) {
    return ast.CreateDeclRef(expr->getDecl());
  }

  clang::Expr *VisitInitListExpr(clang::InitListExpr *expr) {
    std::vector<clang::Expr *> inits;
    for (auto init : expr->inits()) {
      inits.push_back(Clone(init));
    }
    return ast.CreateInitList(inits);
  }

  clang::Expr *VisitCompoundLiteralExpr(clang::CompoundLiteralExpr *expr) {
    return ast.CreateCompoundLit(expr->getType(),
                                 Clone(expr->getInitializer()));
  }

  clang::Expr *VisitParenExpr(clang::ParenExpr *expr) {
    return ast.CreateParen(Clone(expr->getSubExpr()));
  }

  clang::Expr *VisitStmt(clang::Stmt *stmt) {
    LOG(FATAL) << "Unexpected statement";
    return nullptr;
  }

  clang::Expr *VisitExpr(clang::Expr *expr) {
    switch (expr->getStmtClass()) {
      default:
        llvm_unreachable("Unknown stmt kind!");
#define ABSTRACT_STMT(STMT)
#define STMT(CLASS, PARENT)       \
  case clang::Stmt::CLASS##Class: \
    THROW() << "Unsupported " #CLASS;
#include <clang/AST/StmtNodes.inc>
    }

    return nullptr;
  }

  clang::Expr *Visit(clang::Stmt *stmt) {
    auto res{clang::StmtVisitor<ExprCloner, clang::Expr *>::Visit(stmt)};
    CopyProvenance(stmt, res, provenance);
    return res;
  }
};

class StmtCloner : public clang::StmtVisitor<StmtCloner, clang::Stmt *> {
  clang::ASTUnit &unit;
  ASTBuilder ast;
  clang::ASTContext &ctx;
  StmtToIRMap &provenance;
  bool shallow;

  clang::Expr *CloneExpr(clang::Expr *e) {
    return shallow ? e : ::rellic::Clone(unit, e, provenance);
  }

  clang::Stmt *Clone(clang::Stmt *s) { return shallow ? s : Visit(s); }

 public:
  StmtCloner(clang::ASTUnit &unit, StmtToIRMap &provenance,
             bool shallow = false)
      : unit(unit),
        ast(unit),
        ctx(unit.getASTContext()),
        provenance(provenance),
        shallow(shallow) {}

  clang::Stmt *VisitCompoundStmt(clang::CompoundStmt *stmt) {
    std::vector<clang::Stmt *> body;
    for (auto child : stmt->body()) {
      body.push_back(Visit(child));
    }

    return ast.CreateCompoundStmt(body);
  }

  clang::Stmt *VisitIfStmt(clang::IfStmt *stmt) {
    return ast.CreateIf(CloneExpr(stmt->getCond()), Clone(stmt->getThen()),
                        stmt->getElse() ? Clone(stmt->getElse()) : nullptr);
  }

  clang::Stmt *VisitWhileStmt(clang::WhileStmt *stmt) {
    return ast.CreateWhile(CloneExpr(stmt->getCond()), Clone(stmt->getBody()));
  }

  clang::Stmt *VisitDoStmt(clang::DoStmt *stmt) {
    return ast.CreateDo(CloneExpr(stmt->getCond()), Clone(stmt->getBody()));
  }

  clang::Stmt *VisitBreakStmt(clang::BreakStmt *) { return ast.CreateBreak(); }

  clang::Stmt *VisitReturnStmt(clang::ReturnStmt *stmt) {
    return ast.CreateReturn(stmt->getRetValue() ? CloneExpr(stmt->getRetValue())
                                                : nullptr);
  }

  clang::Stmt *VisitNullStmt(clang::NullStmt *) { return ast.CreateNullStmt(); }

  clang::Stmt *VisitDeclStmt(clang::DeclStmt *stmt) {
    return ast.CreateDeclStmt(stmt->getDeclGroup());
  }

  clang::Stmt *VisitExpr(clang::Expr *expr) {
    return shallow ? ::rellic::ShallowClone(unit, expr, provenance)
                   : ::rellic::Clone(unit, expr, provenance);
  }

  clang::Stmt *VisitStmt(clang::Stmt *stmt) {
    switch (stmt->getStmtClass()) {
      default:
        llvm_unreachable("Unknown stmt kind!");
#define ABSTRACT_STMT(STMT)
#define STMT(CLASS, PARENT)       \
  case clang::Stmt::CLASS##Class: \
    THROW() << "Unsupported " #CLASS;
#include <clang/AST/StmtNodes.inc>
    }

    return nullptr;
  }

  clang::Stmt *Visit(clang::Stmt *stmt) {
    if (!stmt) {
      return nullptr;
    }
    auto res{clang::StmtVisitor<StmtCloner, clang::Stmt *>::Visit(stmt)};
    CopyProvenance(stmt, res, provenance);
    return res;
  }
};

clang::Expr *Clone(clang::ASTUnit &unit, clang::Expr *expr,
                   StmtToIRMap &provenance) {
  ExprCloner cloner{unit, provenance};
  return CHECK_NOTNULL(cloner.Visit(CHECK_NOTNULL(expr)));
}

clang::Stmt *Clone(clang::ASTUnit &unit, clang::Stmt *stmt,
                   StmtToIRMap &provenance) {
  StmtCloner cloner{unit, provenance};
  return CHECK_NOTNULL(cloner.Visit(CHECK_NOTNULL(stmt)));
}

clang::Expr *ShallowClone(clang::ASTUnit &unit, clang::Expr *expr,
                          StmtToIRMap &provenance) {
  ExprCloner cloner{unit, provenance, /*shallow=*/true};
  return CHECK_NOTNULL(cloner.Visit(CHECK_NOTNULL(expr)));
}

clang::Stmt *ShallowClone(clang::ASTUnit &unit, clang::Stmt *stmt,
                          StmtToIRMap &provenance) {
  StmtCloner cloner{unit, provenance, /*shallow=*/true};
  return CHECK_NOTNULL(cloner.Visit(CHECK_NOTNULL(stmt)));
}
}  // namespace rellic
