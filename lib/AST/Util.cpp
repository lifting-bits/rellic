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
#include <clang/AST/Stmt.h>
#include <clang/AST/StmtVisitor.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/ASTBuilder.h"
#include "rellic/Exception.h"

namespace rellic {

unsigned GetHash(clang::ASTContext &ctx, clang::Stmt *stmt) {
  llvm::FoldingSetNodeID id;
  stmt->Profile(id, ctx, /*Canonical=*/true);
  return id.ComputeHash();
}

static bool IsEquivalent(clang::ASTContext &ctx, clang::Stmt *a, clang::Stmt *b,
                         llvm::FoldingSetNodeID &foldingSetA,
                         llvm::FoldingSetNodeID &foldingSetB) {
  if (a == b) {
    return true;
  }

  if (a->getStmtClass() != b->getStmtClass()) {
    return false;
  }

  foldingSetA.clear();
  foldingSetB.clear();
  a->Profile(foldingSetA, ctx, /*Canonical=*/true);
  b->Profile(foldingSetB, ctx, /*Canonical=*/true);

  if (foldingSetA != foldingSetB) {
    return false;
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
    } else if (!IsEquivalent(ctx, *child_a, *child_b, foldingSetA,
                             foldingSetB)) {
      return false;
    }

    ++child_a;
    ++child_b;
  }
}

bool IsEquivalent(clang::ASTContext &ctx, clang::Stmt *a, clang::Stmt *b) {
  llvm::FoldingSetNodeID idA, idB;
  return IsEquivalent(ctx, a, b, idA, idB);
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

 public:
  ExprCloner(clang::ASTUnit &unit, StmtToIRMap &provenance)
      : ast(unit), ctx(unit.getASTContext()), provenance(provenance) {}

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
    return ast.CreateCStyleCast(expr->getType(), Visit(expr->getSubExpr()));
  }

  clang::Expr *VisitImplicitCastExpr(clang::ImplicitCastExpr *expr) {
    return Visit(expr->getSubExpr());
  }

  clang::Expr *VisitUnaryOperator(clang::UnaryOperator *expr) {
    return ast.CreateUnaryOp(expr->getOpcode(), Visit(expr->getSubExpr()));
  }

  clang::Expr *VisitBinaryOperator(clang::BinaryOperator *expr) {
    return ast.CreateBinaryOp(expr->getOpcode(), Visit(expr->getLHS()),
                              Visit(expr->getRHS()));
  }

  clang::Expr *VisitConditionalOperator(clang::ConditionalOperator *expr) {
    return ast.CreateConditional(Visit(expr->getCond()),
                                 Visit(expr->getTrueExpr()),
                                 Visit(expr->getFalseExpr()));
  }

  clang::Expr *VisitArraySubscriptExpr(clang::ArraySubscriptExpr *expr) {
    return ast.CreateArraySub(Visit(expr->getBase()), Visit(expr->getIdx()));
  }

  clang::Expr *VisitCallExpr(clang::CallExpr *expr) {
    std::vector<clang::Expr *> args;
    for (auto arg : expr->arguments()) {
      args.push_back(Visit(arg));
    }
    return ast.CreateCall(Visit(expr->getCallee()), args);
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
      inits.push_back(Visit(init));
    }
    return ast.CreateInitList(inits);
  }

  clang::Expr *VisitCompoundLiteralExpr(clang::CompoundLiteralExpr *expr) {
    return ast.CreateCompoundLit(expr->getType(),
                                 Visit(expr->getInitializer()));
  }

  clang::Expr *VisitParenExpr(clang::ParenExpr *expr) {
    return ast.CreateParen(Visit(expr->getSubExpr()));
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

clang::Expr *Clone(clang::ASTUnit &unit, clang::Expr *expr,
                   StmtToIRMap &provenance) {
  ExprCloner cloner{unit, provenance};
  return CHECK_NOTNULL(cloner.Visit(CHECK_NOTNULL(expr)));
}
}  // namespace rellic
