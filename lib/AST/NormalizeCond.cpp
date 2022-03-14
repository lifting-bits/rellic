/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/NormalizeCond.h"

#include <clang/AST/Expr.h>
#include <clang/AST/OperationKinds.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Sema/Sema.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/ASTBuilder.h"
#include "rellic/AST/InferenceRule.h"

namespace rellic {

namespace {

using namespace clang::ast_matchers;

static const auto zero_int_lit = integerLiteral(equals(0));

static inline std::string GetOperatorName(clang::BinaryOperator::Opcode op) {
  return op == clang::BO_LAnd ? "&&" : "||";
}

class DeMorganRule : public InferenceRule {
  clang::BinaryOperator::Opcode to;

 public:
  DeMorganRule(clang::BinaryOperator::Opcode from,
               clang::BinaryOperator::Opcode to)
      : InferenceRule(
            unaryOperator(hasOperatorName("!"),
                          has(ignoringParenImpCasts(binaryOperator(
                              hasOperatorName(GetOperatorName(from))))))
                .bind("not")),
        to(to) {}

  void run(const MatchFinder::MatchResult &result) {
    match = result.Nodes.getNodeAs<clang::UnaryOperator>("not");
  }

  clang::Stmt *GetOrCreateSubstitution(StmtToIRMap &provenance,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) {
    auto &ctx{unit.getASTContext()};
    ASTBuilder ast{unit};

    auto unop{clang::cast<clang::UnaryOperator>(stmt)};
    CHECK(unop == match)
        << "Substituted UnaryOperator is not the matched UnaryOperator";
    auto binop{clang::cast<clang::BinaryOperator>(
        unop->getSubExpr()->IgnoreParenImpCasts())};

    auto new_lhs{ast.CreateLNot(binop->getLHS())};
    auto new_rhs{ast.CreateLNot(binop->getRHS())};
    CopyProvenance(binop->getLHS(), new_lhs, provenance);
    CopyProvenance(binop->getRHS(), new_rhs, provenance);
    return ast.CreateBinaryOp(to, new_lhs, new_rhs);
  }
};

class AssociativeRule : public InferenceRule {
  clang::BinaryOperator::Opcode op;

 public:
  AssociativeRule(clang::BinaryOperator::Opcode op)
      : InferenceRule(
            binaryOperator(hasOperatorName(GetOperatorName(op)),
                           hasRHS(ignoringParenImpCasts(binaryOperator(
                               hasOperatorName(GetOperatorName(op))))))
                .bind("binop")),
        op(op) {}

  void run(const MatchFinder::MatchResult &result) {
    match = result.Nodes.getNodeAs<clang::UnaryOperator>("binop");
  }

  clang::Stmt *GetOrCreateSubstitution(StmtToIRMap &provenance,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) {
    auto &ctx{unit.getASTContext()};
    ASTBuilder ast{unit};

    auto outer{clang::cast<clang::BinaryOperator>(stmt)};
    CHECK(outer == match)
        << "Substituted BinaryOperator is not the matched BinaryOperator";
    auto inner{clang::cast<clang::BinaryOperator>(
        outer->getRHS()->IgnoreParenImpCasts())};

    auto new_lhs{ast.CreateBinaryOp(op, outer->getLHS(), inner->getLHS())};
    auto new_rhs{inner->getRHS()};
    return ast.CreateBinaryOp(op, new_lhs, new_rhs);
  }
};

class LDistributiveRule : public InferenceRule {
 public:
  LDistributiveRule()
      : InferenceRule(
            binaryOperator(hasOperatorName("||"),
                           hasLHS(ignoringParenImpCasts(
                               binaryOperator(hasOperatorName("&&")))))
                .bind("binop")) {}

  void run(const MatchFinder::MatchResult &result) {
    match = result.Nodes.getNodeAs<clang::BinaryOperator>("binop");
  }

  clang::Stmt *GetOrCreateSubstitution(StmtToIRMap &provenance,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) {
    auto &ctx{unit.getASTContext()};
    ASTBuilder ast{unit};

    auto outer{clang::cast<clang::BinaryOperator>(stmt)};
    CHECK(outer == match)
        << "Substituted BinaryOperator is not the matched BinaryOperator";
    auto inner{clang::cast<clang::BinaryOperator>(
        outer->getLHS()->IgnoreParenImpCasts())};

    auto new_lhs{ast.CreateLOr(inner->getLHS(), outer->getRHS())};
    auto new_rhs{ast.CreateLOr(inner->getRHS(), outer->getRHS())};
    return ast.CreateLAnd(new_lhs, new_rhs);
  }
};

class RDistributiveRule : public InferenceRule {
 public:
  RDistributiveRule()
      : InferenceRule(
            binaryOperator(hasOperatorName("||"),
                           hasRHS(ignoringParenImpCasts(
                               binaryOperator(hasOperatorName("&&")))))
                .bind("binop")) {}

  void run(const MatchFinder::MatchResult &result) {
    match = result.Nodes.getNodeAs<clang::BinaryOperator>("binop");
  }

  clang::Stmt *GetOrCreateSubstitution(StmtToIRMap &provenance,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) {
    auto &ctx{unit.getASTContext()};
    ASTBuilder ast{unit};

    auto outer{clang::cast<clang::BinaryOperator>(stmt)};
    CHECK(outer == match)
        << "Substituted BinaryOperator is not the matched BinaryOperator";
    auto inner{clang::cast<clang::BinaryOperator>(
        outer->getRHS()->IgnoreParenImpCasts())};

    auto new_lhs{ast.CreateLOr(outer->getLHS(), inner->getLHS())};
    auto new_rhs{ast.CreateLOr(outer->getLHS(), inner->getRHS())};
    return ast.CreateLAnd(new_lhs, new_rhs);
  }
};

}  // namespace

NormalizeCond::NormalizeCond(StmtToIRMap &provenance, clang::ASTUnit &u,
                             Substitutions &substitutions)
    : ASTPass(provenance, u, substitutions) {}

void NormalizeCond::VisitUnaryOperator(clang::UnaryOperator *op) {
  std::vector<std::unique_ptr<InferenceRule>> rules;

  rules.emplace_back(new DeMorganRule(clang::BO_LAnd, clang::BO_LOr));
  rules.emplace_back(new DeMorganRule(clang::BO_LOr, clang::BO_LAnd));

  ApplyMatchingRules(provenance, ast_unit, op, rules, substitutions);
}

void NormalizeCond::VisitBinaryOperator(clang::BinaryOperator *op) {
  std::vector<std::unique_ptr<InferenceRule>> rules;

  rules.emplace_back(new AssociativeRule(clang::BO_LAnd));
  rules.emplace_back(new AssociativeRule(clang::BO_LOr));
  rules.emplace_back(new LDistributiveRule);
  rules.emplace_back(new RDistributiveRule);

  ApplyMatchingRules(provenance, ast_unit, op, rules, substitutions);
}

void NormalizeCond::RunImpl(clang::Stmt *stmt) {
  LOG(INFO) << "Conversion into conjunctive normal form";
  Visit(stmt);
}

}  // namespace rellic