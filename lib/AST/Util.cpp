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

#include <numeric>

#include "rellic/AST/ASTBuilder.h"
#include "rellic/Exception.h"

namespace rellic {

unsigned GetHash(clang::ASTContext &ctx, clang::Stmt *stmt) {
  llvm::FoldingSetNodeID id;
  stmt->Profile(id, ctx, /*Canonical=*/true);
  return id.ComputeHash();
}

class EqualityVisitor
    : public clang::StmtVisitor<EqualityVisitor, bool, clang::Expr *> {
 private:
 public:
  bool VisitIntegerLiteral(clang::IntegerLiteral *expr, clang::Expr *other) {
    if (auto other_expr = clang::dyn_cast<clang::IntegerLiteral>(other)) {
      return expr->getValue() == other_expr->getValue();
    }
    return false;
  }

  bool VisitCharacterLiteral(clang::CharacterLiteral *expr,
                             clang::Expr *other) {
    if (auto other_expr = clang::dyn_cast<clang::CharacterLiteral>(other)) {
      return expr->getValue() == other_expr->getValue();
    }
    return false;
  }

  bool VisitStringLiteral(clang::StringLiteral *expr, clang::Expr *other) {
    if (auto other_expr = clang::dyn_cast<clang::StringLiteral>(other)) {
      return expr->getString() == other_expr->getString();
    }
    return false;
  }

  bool VisitFloatingLiteral(clang::FloatingLiteral *expr, clang::Expr *other) {
    if (auto other_expr = clang::dyn_cast<clang::FloatingLiteral>(other)) {
      return expr->getValue() == other_expr->getValue();
    }
    return false;
  }

  bool VisitCastExpr(clang::CastExpr *expr, clang::Expr *other) {
    if (auto other_expr = clang::dyn_cast<clang::CastExpr>(other)) {
      return expr->getType() == other_expr->getType() &&
             Visit(expr->getSubExpr(), other_expr->getSubExpr());
    }
    return false;
  }

  bool VisitImplicitCastExpr(clang::ImplicitCastExpr *expr,
                             clang::Expr *other) {
    if (auto other_expr = clang::dyn_cast<clang::ImplicitCastExpr>(other)) {
      return expr->getType() == other_expr->getType() &&
             Visit(expr->getSubExpr(), other_expr->getSubExpr());
    }
    return false;
  }

  bool VisitUnaryOperator(clang::UnaryOperator *expr, clang::Expr *other) {
    if (auto other_expr = clang::dyn_cast<clang::UnaryOperator>(other)) {
      return expr->getOpcode() == other_expr->getOpcode() &&
             Visit(expr->getSubExpr(), other_expr->getSubExpr());
    }
    return false;
  }

  bool VisitBinaryOperator(clang::BinaryOperator *expr, clang::Expr *other) {
    if (auto other_expr = clang::dyn_cast<clang::BinaryOperator>(other)) {
      return expr->getOpcode() == other_expr->getOpcode() &&
             Visit(expr->getLHS(), other_expr->getLHS()) &&
             Visit(expr->getRHS(), other_expr->getRHS());
    }
    return false;
  }

  bool VisitConditionalOperator(clang::ConditionalOperator *expr,
                                clang::Expr *other) {
    if (auto other_expr = clang::dyn_cast<clang::ConditionalOperator>(other)) {
      return Visit(expr->getCond(), other_expr->getCond()) &&
             Visit(expr->getTrueExpr(), other_expr->getTrueExpr()) &&
             Visit(expr->getFalseExpr(), other_expr->getFalseExpr());
    }
    return false;
  }

  bool VisitArraySubscriptExpr(clang::ArraySubscriptExpr *expr,
                               clang::Expr *other) {
    if (auto other_expr = clang::dyn_cast<clang::ArraySubscriptExpr>(other)) {
      return Visit(expr->getBase(), other_expr->getBase()) &&
             Visit(expr->getIdx(), other_expr->getIdx());
    }
    return false;
  }

  bool VisitCallExpr(clang::CallExpr *expr, clang::Expr *other) {
    if (auto other_expr = clang::dyn_cast<clang::CallExpr>(other)) {
      auto child_a{expr->arg_begin()};
      auto child_b{other_expr->arg_begin()};
      while (true) {
        bool a_end{child_a == expr->arg_end()};
        bool b_end{child_b == other_expr->arg_end()};
        if (a_end != b_end) {
          return false;
        } else if (a_end && b_end) {
          break;
        } else if (!Visit(*child_a, *child_b)) {
          return false;
        }

        ++child_a;
        ++child_b;
      }

      return Visit(expr->getCallee(), other_expr->getCallee());
    }
    return false;
  }

  bool VisitMemberExpr(clang::MemberExpr *expr, clang::Expr *other) {
    if (auto other_expr = clang::dyn_cast<clang::MemberExpr>(other)) {
      return expr->isArrow() == other_expr->isArrow() &&
             expr->getMemberDecl() == other_expr->getMemberDecl() &&
             expr->getType() == other_expr->getType() &&
             Visit(expr->getBase(), other_expr->getBase());
    }
    return false;
  }

  bool VisitDeclRefExpr(clang::DeclRefExpr *expr, clang::Expr *other) {
    if (auto other_expr = clang::dyn_cast<clang::DeclRefExpr>(other)) {
      return expr->getDecl() == other_expr->getDecl();
    }
    return false;
  }

  bool VisitInitListExpr(clang::InitListExpr *expr, clang::Expr *other) {
    if (auto other_expr = clang::dyn_cast<clang::InitListExpr>(other)) {
      if (expr->getNumInits() != other_expr->getNumInits()) {
        return false;
      }

      auto inits{expr->getInits()};
      auto other_inits{other_expr->getInits()};
      for (auto i{0U}; i < expr->getNumInits(); ++i) {
        if (!Visit(inits[i], other_inits[i])) {
          return false;
        }
      }
      return true;
    }
    return false;
  }

  bool VisitCompoundLiteralExpr(clang::CompoundLiteralExpr *expr,
                                clang::Expr *other) {
    if (auto other_expr = clang::dyn_cast<clang::CompoundLiteralExpr>(other)) {
      return expr->getType() == other_expr->getType() &&
             Visit(expr->getInitializer(), other_expr->getInitializer());
    }
    return false;
  }

  bool VisitParenExpr(clang::ParenExpr *expr, clang::Expr *other) {
    if (auto other_expr = clang::dyn_cast<clang::ParenExpr>(other)) {
      return Visit(expr->getSubExpr(), other_expr->getSubExpr());
    }
    return false;
  }
};

bool IsEquivalent(clang::Expr *a, clang::Expr *b) {
  EqualityVisitor ev;
  return ev.Visit(a, b);
}

class ExprCloner : public clang::StmtVisitor<ExprCloner, clang::Expr *> {
  ASTBuilder ast;
  clang::ASTContext &ctx;
  DecompilationContext::ExprToUseMap &provenance;

 public:
  ExprCloner(clang::ASTUnit &unit,
             DecompilationContext::ExprToUseMap &provenance)
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
    auto expr{clang::dyn_cast<clang::Expr>(stmt)};
    auto res{clang::StmtVisitor<ExprCloner, clang::Expr *>::Visit(stmt)};
    CopyProvenance(expr, res, provenance);
    return res;
  }
};

clang::Expr *Clone(clang::ASTUnit &unit, clang::Expr *expr,
                   DecompilationContext::ExprToUseMap &provenance) {
  ExprCloner cloner{unit, provenance};
  return CHECK_NOTNULL(cloner.Visit(CHECK_NOTNULL(expr)));
}

std::string ClangThingToString(const clang::Stmt *stmt) {
  std::string s;
  llvm::raw_string_ostream os(s);
  stmt->printPretty(os, nullptr, clang::PrintingPolicy(clang::LangOptions()));
  return s;
}

z3::goal ApplyTactic(const z3::tactic &tactic, z3::expr expr) {
  z3::goal goal(tactic.ctx());
  goal.add(expr.simplify());
  auto app{tactic(goal)};
  CHECK(app.size() == 1) << "Unexpected multiple goals in application!";
  return app[0];
}

bool Prove(z3::expr expr) {
  return ApplyTactic(z3::tactic(expr.ctx(), "sat"), !(expr).simplify())
      .is_decided_unsat();
}

z3::expr HeavySimplify(z3::expr expr) {
  if (Prove(expr)) {
    return expr.ctx().bool_val(true);
  }

  z3::tactic aig(expr.ctx(), "aig");
  z3::tactic simplify(expr.ctx(), "simplify");
  z3::tactic ctx_solver_simplify(expr.ctx(), "ctx-solver-simplify");
  auto tactic{simplify & aig & ctx_solver_simplify};
  return ApplyTactic(tactic, expr).as_expr();
}

z3::expr_vector Clone(z3::expr_vector &vec) {
  z3::expr_vector clone{vec.ctx()};
  for (auto expr : vec) {
    clone.push_back(expr);
  }

  return clone;
}

z3::expr OrderById(z3::expr expr) {
  if (expr.is_and() || expr.is_or()) {
    std::vector<unsigned> args_indices(expr.num_args(), 0);
    std::iota(args_indices.begin(), args_indices.end(), 0);
    std::sort(args_indices.begin(), args_indices.end(),
              [&expr](unsigned a, unsigned b) {
                return expr.arg(a).id() < expr.arg(b).id();
              });
    z3::expr_vector new_args{expr.ctx()};
    for (auto idx : args_indices) {
      new_args.push_back(OrderById(expr.arg(idx)));
    }
    if (expr.is_and()) {
      return z3::mk_and(new_args);
    } else {
      return z3::mk_or(new_args);
    }
  }

  if (expr.is_not()) {
    return !OrderById(expr.arg(0));
  }

  return expr;
}

unsigned DecompilationContext::InsertZExpr(const z3::expr &e) {
  auto idx{z3_exprs.size()};
  z3_exprs.push_back(e);
  return idx;
}
}  // namespace rellic
