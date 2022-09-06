/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/LoopRefine.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <algorithm>
#include <iterator>

#include "rellic/AST/ASTBuilder.h"
#include "rellic/AST/InferenceRule.h"
#include "rellic/AST/Util.h"

namespace rellic {

namespace {

using namespace clang::ast_matchers;

// Matches `while(1)`, `if(1)`, etc.
static const auto cond_true{hasCondition(integerLiteral(equals(true)))};
// Matches `{ break; }`
static const auto comp_break{
    compoundStmt(has(breakStmt()), statementCountIs(1))};

class WhileRule : public InferenceRule {
 public:
  WhileRule()
      : InferenceRule(whileStmt(
            stmt().bind("while"), cond_true,
            hasBody(compoundStmt(
                has(ifStmt(stmt().bind("if"), hasThen(comp_break))))))) {}

  void run(const MatchFinder::MatchResult &result) override {
    auto loop{result.Nodes.getNodeAs<clang::WhileStmt>("while")};
    auto body{clang::cast<clang::CompoundStmt>(loop->getBody())};
    auto ifstmt{result.Nodes.getNodeAs<clang::IfStmt>("if")};
    if (body->body_front() == ifstmt) {
      match = loop;
    }
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) override {
    auto loop{clang::dyn_cast<clang::WhileStmt>(stmt)};

    CHECK(loop && loop == match)
        << "Substituted WhileStmt is not the matched WhileStmt!";

    auto comp{clang::cast<clang::CompoundStmt>(loop->getBody())};
    auto ifstmt{clang::cast<clang::IfStmt>(comp->body_front())};
    std::vector<clang::Stmt *> new_body;
    if (auto else_stmt = ifstmt->getElse()) {
      new_body.push_back(else_stmt);
    }
    std::copy(comp->body_begin() + 1, comp->body_end(),
              std::back_inserter(new_body));
    ASTBuilder ast(unit);
    auto new_while{
        ast.CreateWhile(dec_ctx.marker_expr, ast.CreateCompoundStmt(new_body))};
    dec_ctx.conds[new_while] =
        dec_ctx.InsertZExpr(!dec_ctx.z3_exprs[dec_ctx.conds[ifstmt]]);
    return new_while;
  }
};

class ElseWhileRule : public InferenceRule {
 public:
  ElseWhileRule()
      : InferenceRule(whileStmt(
            stmt().bind("while"), cond_true,
            hasBody(compoundStmt(
                has(ifStmt(stmt().bind("if"), hasElse(comp_break))))))) {}

  void run(const MatchFinder::MatchResult &result) override {
    auto loop{result.Nodes.getNodeAs<clang::WhileStmt>("while")};
    auto body{clang::cast<clang::CompoundStmt>(loop->getBody())};
    auto ifstmt{result.Nodes.getNodeAs<clang::IfStmt>("if")};
    if (body->body_front() == ifstmt) {
      match = loop;
    }
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) override {
    auto loop{clang::dyn_cast<clang::WhileStmt>(stmt)};

    CHECK(loop && loop == match)
        << "Substituted WhileStmt is not the matched WhileStmt!";

    auto comp{clang::cast<clang::CompoundStmt>(loop->getBody())};
    auto ifstmt{clang::cast<clang::IfStmt>(comp->body_front())};
    std::vector<clang::Stmt *> new_body;
    ASTBuilder ast(unit);
    auto new_while{
        ast.CreateWhile(dec_ctx.marker_expr, ast.CreateCompoundStmt(new_body))};
    dec_ctx.conds[new_while] = dec_ctx.conds[ifstmt];
    return new_while;
  }
};

class DoWhileRule : public InferenceRule {
 public:
  DoWhileRule()
      : InferenceRule(whileStmt(
            stmt().bind("while"), cond_true,
            hasBody(compoundStmt(
                has(ifStmt(stmt().bind("if"), hasThen(comp_break))))))) {}

  void run(const MatchFinder::MatchResult &result) override {
    auto loop{result.Nodes.getNodeAs<clang::WhileStmt>("while")};
    auto body{clang::cast<clang::CompoundStmt>(loop->getBody())};
    auto ifstmt{result.Nodes.getNodeAs<clang::IfStmt>("if")};
    if (body->body_back() == ifstmt) {
      match = loop;
    }
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) override {
    auto loop{clang::dyn_cast<clang::WhileStmt>(stmt)};

    CHECK(loop && loop == match)
        << "Substituted WhileStmt is not the matched WhileStmt!";

    auto comp{clang::cast<clang::CompoundStmt>(loop->getBody())};
    auto ifstmt{clang::cast<clang::IfStmt>(comp->body_back())};
    auto cond{dec_ctx.z3_exprs[dec_ctx.conds[ifstmt]]};
    std::vector<clang::Stmt *> new_body(comp->body_begin(),
                                        comp->body_end() - 1);
    ASTBuilder ast(unit);
    if (auto else_stmt = ifstmt->getElse()) {
      auto new_if{ast.CreateIf(dec_ctx.marker_expr, else_stmt)};
      dec_ctx.conds[new_if] = dec_ctx.z3_exprs.size();
      new_body.push_back(new_if);
    }
    auto new_do{
        ast.CreateDo(dec_ctx.marker_expr, ast.CreateCompoundStmt(new_body))};
    dec_ctx.conds[new_do] = dec_ctx.InsertZExpr(!cond);
    return new_do;
  }
};

class ElseDoWhileRule : public InferenceRule {
 public:
  ElseDoWhileRule()
      : InferenceRule(whileStmt(
            stmt().bind("while"), cond_true,
            hasBody(compoundStmt(
                has(ifStmt(stmt().bind("if"), hasElse(comp_break))))))) {}

  void run(const MatchFinder::MatchResult &result) override {
    auto loop{result.Nodes.getNodeAs<clang::WhileStmt>("while")};
    auto body{clang::cast<clang::CompoundStmt>(loop->getBody())};
    auto ifstmt{result.Nodes.getNodeAs<clang::IfStmt>("if")};
    if (body->body_back() == ifstmt) {
      match = loop;
    }
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) override {
    auto loop{clang::dyn_cast<clang::WhileStmt>(stmt)};

    CHECK(loop && loop == match)
        << "Substituted WhileStmt is not the matched WhileStmt!";

    auto comp{clang::cast<clang::CompoundStmt>(loop->getBody())};
    auto ifstmt{clang::cast<clang::IfStmt>(comp->body_back())};
    std::vector<clang::Stmt *> new_body(comp->body_begin(),
                                        comp->body_end() - 1);
    ASTBuilder ast(unit);

    ifstmt->setElse(nullptr);
    new_body.push_back(ifstmt);

    auto new_do{
        ast.CreateDo(dec_ctx.marker_expr, ast.CreateCompoundStmt(new_body))};
    dec_ctx.conds[new_do] = dec_ctx.conds[ifstmt];
    return new_do;
  }
};

class NestedDoWhileRule : public InferenceRule {
 private:
  bool matched = false;

 public:
  NestedDoWhileRule()
      : InferenceRule(
            whileStmt(stmt().bind("while"), cond_true,
                      hasBody(compoundStmt(findAll(ifStmt(
                          stmt().bind("if"), hasThen(has(breakStmt())))))))) {}

  void run(const MatchFinder::MatchResult &result) override {
    if (!matched) {
      auto loop{result.Nodes.getNodeAs<clang::WhileStmt>("while")};
      auto body{clang::cast<clang::CompoundStmt>(loop->getBody())};
      auto ifstmt{result.Nodes.getNodeAs<clang::IfStmt>("if")};
      if (body->body_back() == ifstmt) {
        match = loop;
      }
    } else {
      match = nullptr;
    }
    matched = true;
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) override {
    auto loop{clang::dyn_cast<clang::WhileStmt>(stmt)};

    CHECK(loop && loop == match)
        << "Substituted WhileStmt is not the matched WhileStmt!";
    auto comp{clang::cast<clang::CompoundStmt>(loop->getBody())};
    auto if_stmt{clang::cast<clang::IfStmt>(comp->body_back())};
    auto cond{dec_ctx.z3_exprs[dec_ctx.conds[if_stmt]]};

    std::vector<clang::Stmt *> do_body(comp->body_begin(),
                                       comp->body_end() - 1);
    ASTBuilder ast(unit);
    if (auto else_stmt = if_stmt->getElse()) {
      auto new_if{ast.CreateIf(dec_ctx.marker_expr, else_stmt)};
      dec_ctx.conds[new_if] = dec_ctx.z3_exprs.size();
      do_body.push_back(new_if);
    }

    auto do_stmt{
        ast.CreateDo(dec_ctx.marker_expr, ast.CreateCompoundStmt(do_body))};
    dec_ctx.conds[do_stmt] = dec_ctx.InsertZExpr(!cond);

    std::vector<clang::Stmt *> while_body({do_stmt, if_stmt->getThen()});
    auto new_while{ast.CreateWhile(dec_ctx.marker_expr,
                                   ast.CreateCompoundStmt(while_body))};
    dec_ctx.conds[new_while] = dec_ctx.conds[loop];
    return new_while;
  }
};

class LoopToSeq : public InferenceRule {
 public:
  LoopToSeq()
      : InferenceRule(
            whileStmt(stmt().bind("while"), cond_true,
                      hasBody(compoundStmt(hasAnySubstatement(anyOf(
                          ifStmt(stmt().bind("if"), hasThen(has(breakStmt())),
                                 hasElse(has(breakStmt()))),
                          breakStmt())))))) {}

  void run(const MatchFinder::MatchResult &result) override {
    auto loop{result.Nodes.getNodeAs<clang::WhileStmt>("while")};
    if (auto ifstmt = result.Nodes.getNodeAs<clang::IfStmt>("if")) {
      auto body = clang::cast<clang::CompoundStmt>(loop->getBody());
      if (body->body_back() == ifstmt) {
        match = loop;
      }
    } else {
      match = loop;
    }
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) override {
    auto loop = clang::dyn_cast<clang::WhileStmt>(stmt);

    CHECK(loop && loop == match)
        << "Substituted WhileStmt is not the matched WhileStmt!";

    auto loop_body{clang::cast<clang::CompoundStmt>(loop->getBody())};

    std::vector<clang::Stmt *> new_body(loop_body->body_begin(),
                                        loop_body->body_end());

    ASTBuilder ast(unit);
    if (auto ifstmt = clang::dyn_cast<clang::IfStmt>(loop_body->body_back())) {
      std::vector<clang::Stmt *> branches(
          {ifstmt->getThen(), ifstmt->getElse()});
      for (auto &branch : branches) {
        std::vector<clang::Stmt *> new_branch_body;
        if (auto branch_body = clang::dyn_cast<clang::CompoundStmt>(branch)) {
          for (auto stmt : branch_body->body()) {
            if (clang::isa<clang::BreakStmt>(stmt)) {
              break;
            }
            new_branch_body.push_back(stmt);
          }
        }
        branch = ast.CreateCompoundStmt(new_branch_body);
      }
      ifstmt->setThen(branches[0]);
      ifstmt->setElse(branches[1]);
    } else {
      new_body.pop_back();
    }

    return ast.CreateCompoundStmt(new_body);
  }
};

static const auto has_break = hasDescendant(breakStmt());

class CondToSeqRule : public InferenceRule {
 public:
  CondToSeqRule()
      : InferenceRule(whileStmt(
            stmt().bind("while"), cond_true,
            hasBody(compoundStmt(
                has(ifStmt(hasThen(unless(has_break)), hasElse(has_break))),
                statementCountIs(1))))) {}

  void run(const MatchFinder::MatchResult &result) override {
    match = result.Nodes.getNodeAs<clang::WhileStmt>("while");
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) override {
    auto loop{clang::dyn_cast<clang::WhileStmt>(stmt)};

    CHECK(loop && loop == match)
        << "Substituted WhileStmt is not the matched WhileStmt!";

    ASTBuilder ast(unit);
    auto body{clang::cast<clang::CompoundStmt>(loop->getBody())};
    auto ifstmt{clang::cast<clang::IfStmt>(body->body_front())};
    auto inner_loop{ast.CreateWhile(dec_ctx.marker_expr, ifstmt->getThen())};
    dec_ctx.conds[inner_loop] = dec_ctx.conds[ifstmt];
    std::vector<clang::Stmt *> new_body({inner_loop});
    if (auto comp = clang::dyn_cast<clang::CompoundStmt>(ifstmt->getElse())) {
      new_body.insert(new_body.end(), comp->body_begin(), comp->body_end());
    } else {
      new_body.push_back(ifstmt->getElse());
    }
    auto new_while{
        ast.CreateWhile(dec_ctx.marker_expr, ast.CreateCompoundStmt(new_body))};
    dec_ctx.conds[new_while] = dec_ctx.conds[loop];
    return new_while;
  }
};

class CondToSeqNegRule : public InferenceRule {
 public:
  CondToSeqNegRule()
      : InferenceRule(whileStmt(
            stmt().bind("while"), cond_true,
            hasBody(compoundStmt(
                has(ifStmt(hasThen(has_break), hasElse(unless(has_break)))),
                statementCountIs(1))))) {}

  void run(const MatchFinder::MatchResult &result) override {
    match = result.Nodes.getNodeAs<clang::WhileStmt>("while");
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) override {
    auto loop{clang::dyn_cast<clang::WhileStmt>(stmt)};

    CHECK(loop && loop == match)
        << "Substituted WhileStmt is not the matched WhileStmt!";

    ASTBuilder ast(unit);
    auto body{clang::cast<clang::CompoundStmt>(loop->getBody())};
    auto ifstmt{clang::cast<clang::IfStmt>(body->body_front())};
    auto cond{dec_ctx.z3_exprs[dec_ctx.conds[ifstmt]]};
    auto inner_loop{ast.CreateWhile(dec_ctx.marker_expr, ifstmt->getElse())};
    dec_ctx.conds[inner_loop] = dec_ctx.InsertZExpr(!cond);
    std::vector<clang::Stmt *> new_body({inner_loop});
    if (auto comp = clang::dyn_cast<clang::CompoundStmt>(ifstmt->getThen())) {
      new_body.insert(new_body.end(), comp->body_begin(), comp->body_end());
    } else {
      new_body.push_back(ifstmt->getThen());
    }

    auto new_while{
        ast.CreateWhile(dec_ctx.marker_expr, ast.CreateCompoundStmt(new_body))};
    dec_ctx.conds[new_while] = dec_ctx.conds[loop];
    return new_while;
  }
};

}  // namespace

LoopRefine::LoopRefine(DecompilationContext &dec_ctx, clang::ASTUnit &u)
    : TransformVisitor<LoopRefine>(dec_ctx, u) {}

bool LoopRefine::VisitWhileStmt(clang::WhileStmt *loop) {
  // DLOG(INFO) << "VisitWhileStmt";

  // If a while statements has an unconditional break in it, it can be
  // substituted for an if statement and all statements after the break can be
  // ignored.
  auto body{clang::cast<clang::CompoundStmt>(loop->getBody())};
  auto break_stmt{std::find_if(
      body->body_begin(), body->body_end(),
      [](auto stmt) { return clang::isa<clang::BreakStmt>(stmt); })};
  if (break_stmt != body->body_end()) {
    std::vector<clang::Stmt *> new_body_stmts{body->body_begin(), break_stmt};
    substitutions[loop] =
        ast.CreateIf(loop->getCond(), ast.CreateCompoundStmt(new_body_stmts));
    return !Stopped();
  }

  std::vector<std::unique_ptr<InferenceRule>> rules;

  rules.emplace_back(new CondToSeqRule);
  rules.emplace_back(new CondToSeqNegRule);
  rules.emplace_back(new NestedDoWhileRule);
  rules.emplace_back(new LoopToSeq);
  rules.emplace_back(new WhileRule);
  rules.emplace_back(new DoWhileRule);
  rules.emplace_back(new ElseWhileRule);
  rules.emplace_back(new ElseDoWhileRule);

  auto sub{ApplyFirstMatchingRule(dec_ctx, ast_unit, loop, rules)};
  if (sub != loop) {
    substitutions[loop] = sub;
  }

  return !Stopped();
}

void LoopRefine::RunImpl() {
  LOG(INFO) << "Rule-based loop refinement";
  TransformVisitor<LoopRefine>::RunImpl();
  TraverseDecl(ast_ctx.getTranslationUnitDecl());
}

}  // namespace rellic