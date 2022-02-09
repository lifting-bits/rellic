/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/ExprCombine.h"

#include <clang/Sema/Sema.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/InferenceRule.h"

namespace rellic {

namespace {

using namespace clang::ast_matchers;

static const auto zero_int_lit = integerLiteral(equals(0));

// Matches `(&base)[0]` and subs it for `base`
class ArraySubscriptAddrOfRule : public InferenceRule {
 public:
  ArraySubscriptAddrOfRule()
      : InferenceRule(arraySubscriptExpr(
            stmt().bind("sub"),
            hasBase(parenExpr(has(unaryOperator(stmt().bind("base"))))),
            hasIndex(zero_int_lit))) {}

  void run(const MatchFinder::MatchResult &result) {
    auto op = result.Nodes.getNodeAs<clang::UnaryOperator>("base");
    if (op->getOpcode() == clang::UO_AddrOf) {
      match = result.Nodes.getNodeAs<clang::ArraySubscriptExpr>("sub");
    }
  }

  clang::Stmt *GetOrCreateSubstitution(StmtToIRMap &provenance,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) {
    auto sub = clang::cast<clang::ArraySubscriptExpr>(stmt);
    CHECK(sub == match) << "Substituted ArraySubscriptExpr is not the matched "
                           "ArraySubscriptExpr!";
    auto paren = clang::cast<clang::ParenExpr>(sub->getBase());
    auto addr_of = clang::cast<clang::UnaryOperator>(paren->getSubExpr());
    return addr_of->getSubExpr();
  }
};

// Matches `&base[0]` and subs it for `base`
class AddrOfArraySubscriptRule : public InferenceRule {
 public:
  AddrOfArraySubscriptRule()
      : InferenceRule(
            unaryOperator(stmt().bind("addr_of"), hasOperatorName("&"),
                          has(ignoringParenImpCasts(
                              arraySubscriptExpr(hasIndex(zero_int_lit)))))) {}

  void run(const MatchFinder::MatchResult &result) {
    match = result.Nodes.getNodeAs<clang::UnaryOperator>("addr_of");
  }

  clang::Stmt *GetOrCreateSubstitution(StmtToIRMap &provenance,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) {
    auto addr_of = clang::cast<clang::UnaryOperator>(stmt);
    CHECK(addr_of == match)
        << "Substituted UnaryOperator is not the matched UnaryOperator!";
    auto subexpr = addr_of->getSubExpr()->IgnoreParenImpCasts();
    auto sub = clang::cast<clang::ArraySubscriptExpr>(subexpr);
    return sub->getBase();
  }
};

// Matches `*&expr` and subs it for `expr`
class DerefAddrOfRule : public InferenceRule {
 public:
  DerefAddrOfRule()
      : InferenceRule(unaryOperator(
            stmt().bind("deref"), hasOperatorName("*"),
            has(ignoringParenImpCasts(unaryOperator(hasOperatorName("&")))))) {}

  void run(const MatchFinder::MatchResult &result) {
    match = result.Nodes.getNodeAs<clang::UnaryOperator>("deref");
  }

  clang::Stmt *GetOrCreateSubstitution(StmtToIRMap &provenance,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) {
    auto deref = clang::cast<clang::UnaryOperator>(stmt);
    CHECK(deref == match)
        << "Substituted UnaryOperator is not the matched UnaryOperator!";
    auto subexpr = deref->getSubExpr()->IgnoreParenImpCasts();
    auto addr_of = clang::cast<clang::UnaryOperator>(subexpr);
    return addr_of->getSubExpr();
  }
};

// Matches `!(comp)` and subs it for `negcomp`
class NegComparisonRule : public InferenceRule {
 public:
  NegComparisonRule()
      : InferenceRule(unaryOperator(
            stmt().bind("not"), hasOperatorName("!"),
            has(ignoringParenImpCasts(binaryOperator(stmt().bind("binop")))))) {
  }

  void run(const MatchFinder::MatchResult &result) {
    auto binop = result.Nodes.getNodeAs<clang::BinaryOperator>("binop");
    if (binop->isComparisonOp()) {
      match = result.Nodes.getNodeAs<clang::UnaryOperator>("not");
    }
  }

  clang::Stmt *GetOrCreateSubstitution(StmtToIRMap &provenance,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) {
    auto op = clang::cast<clang::UnaryOperator>(stmt);
    CHECK(op == match)
        << "Substituted UnaryOperator is not the matched UnaryOperator!";
    auto subexpr = op->getSubExpr()->IgnoreParenImpCasts();
    auto binop = clang::cast<clang::BinaryOperator>(subexpr);
    auto opc = clang::BinaryOperator::negateComparisonOp(binop->getOpcode());
    return ASTBuilder(unit).CreateBinaryOp(opc, binop->getLHS(),
                                           binop->getRHS());
  }
};

// Matches `(a)` and subs it for `a`
class ParenDeclRefExprStripRule : public InferenceRule {
 public:
  ParenDeclRefExprStripRule()
      : InferenceRule(
            parenExpr(stmt().bind("paren"),
                      has(ignoringImpCasts(declRefExpr(to(varDecl())))))) {}

  void run(const MatchFinder::MatchResult &result) {
    match = result.Nodes.getNodeAs<clang::ParenExpr>("paren");
  }

  clang::Stmt *GetOrCreateSubstitution(StmtToIRMap &provenance,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) {
    auto paren = clang::cast<clang::ParenExpr>(stmt);
    CHECK(paren == match)
        << "Substituted ParenExpr is not the matched ParenExpr!";
    return paren->getSubExpr();
  }
};

// Matches `(&expr)->field` and subs it for `expr.field`
class MemberExprAddrOfRule : public InferenceRule {
 public:
  MemberExprAddrOfRule()
      : InferenceRule(memberExpr(
            stmt().bind("arrow"), isArrow(),
            has(expr(stmt().bind("base"), ignoringParenImpCasts(unaryOperator(
                                              hasOperatorName("&"))))))) {}

  void run(const MatchFinder::MatchResult &result) {
    auto arrow{result.Nodes.getNodeAs<clang::MemberExpr>("arrow")};
    if (result.Nodes.getNodeAs<clang::Expr>("base") == arrow->getBase()) {
      match = arrow;
    }
  }

  clang::Stmt *GetOrCreateSubstitution(StmtToIRMap &provenance,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) {
    auto arrow{clang::cast<clang::MemberExpr>(stmt)};
    CHECK(arrow == match)
        << "Substituted MemberExpr is not the matched MemberExpr!";
    auto base{arrow->getBase()->IgnoreParenImpCasts()};
    auto addr_of{clang::cast<clang::UnaryOperator>(base)};
    auto field{clang::dyn_cast<clang::FieldDecl>(arrow->getMemberDecl())};
    CHECK(field != nullptr)
        << "Substituted MemberExpr is not a structure field access!";
    return ASTBuilder(unit).CreateDot(addr_of->getSubExpr(), field);
  }
};

// Matches `expr[0U].field` and subs it for `expr->field`
class MemberExprArraySubRule : public InferenceRule {
 public:
  MemberExprArraySubRule()
      : InferenceRule(memberExpr(
            stmt().bind("dot"), unless(isArrow()),
            has(expr(stmt().bind("base"),
                     ignoringParenImpCasts(
                         arraySubscriptExpr(hasIndex(zero_int_lit))))))) {}

  void run(const MatchFinder::MatchResult &result) {
    auto dot{result.Nodes.getNodeAs<clang::MemberExpr>("dot")};
    if (result.Nodes.getNodeAs<clang::Expr>("base") == dot->getBase()) {
      match = dot;
    }
  }

  clang::Stmt *GetOrCreateSubstitution(StmtToIRMap &provenance,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) {
    auto dot{clang::cast<clang::MemberExpr>(stmt)};
    CHECK(dot == match)
        << "Substituted MemberExpr is not the matched MemberExpr!";
    auto base{dot->getBase()->IgnoreParenImpCasts()};
    auto sub{clang::cast<clang::ArraySubscriptExpr>(base)};
    auto field{clang::dyn_cast<clang::FieldDecl>(dot->getMemberDecl())};
    CHECK(field != nullptr)
        << "Substituted MemberExpr is not a structure field access!";
    return ASTBuilder(unit).CreateArrow(sub->getBase(), field);
  }
};

// Matches `a = (type)expr`, where `a` is of `type` and subs it for `a = expr`
class AssignCastedExprRule : public InferenceRule {
 public:
  AssignCastedExprRule()
      : InferenceRule(binaryOperator(
            stmt().bind("assign"), hasOperatorName("="),
            has(ignoringParenImpCasts(cStyleCastExpr(stmt().bind("cast")))))) {}

  void run(const MatchFinder::MatchResult &result) {
    auto assign{result.Nodes.getNodeAs<clang::BinaryOperator>("assign")};
    auto cast{result.Nodes.getNodeAs<clang::CStyleCastExpr>("cast")};
    if (assign->getType() == cast->getType()) {
      match = assign;
    };
  }

  clang::Stmt *GetOrCreateSubstitution(StmtToIRMap &provenance,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) {
    auto assign{clang::cast<clang::BinaryOperator>(stmt)};
    CHECK(assign == match)
        << "Substituted BinaryOperator is not the matched BinaryOperator!";

    auto lhs{assign->getLHS()};

    auto rhs{clang::cast<clang::CStyleCastExpr>(
        assign->getRHS()->IgnoreParenImpCasts())};

    if (unit.getSema().CheckAssignmentConstraints(
            clang::SourceLocation(), lhs->getType(),
            rhs->getSubExpr()->getType()) ==
        clang::Sema::AssignConvertType::Compatible) {
      return ASTBuilder(unit).CreateAssign(lhs, rhs->getSubExpr());
    }

    return assign;
  }
};

// Matches `(int)(unsigned int)expr` and subs it for `(int)expr`
class UnsignedToSignedCStyleCastRule : public InferenceRule {
 public:
  UnsignedToSignedCStyleCastRule()
      : InferenceRule(cStyleCastExpr(
            stmt().bind("cast"), hasType(isSignedInteger()),
            has(ignoringParenImpCasts(
                cStyleCastExpr(hasType(isUnsignedInteger())))))) {}

  void run(const MatchFinder::MatchResult &result) {
    match = result.Nodes.getNodeAs<clang::CStyleCastExpr>("cast");
  }

  clang::Stmt *GetOrCreateSubstitution(StmtToIRMap &provenance,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) {
    auto cast{clang::cast<clang::CStyleCastExpr>(stmt)};
    CHECK(cast == match)
        << "Substituted CStyleCastExpr is not the matched CStyleCastExpr!";

    auto subcast{clang::cast<clang::CStyleCastExpr>(
        cast->getSubExpr()->IgnoreParenImpCasts())};

    if (unit.getASTContext().getCorrespondingUnsignedType(cast->getType()) ==
        subcast->getType()) {
      return ASTBuilder(unit).CreateCStyleCast(cast->getType(),
                                               subcast->getSubExpr());
    }

    return cast;
  }
};

// Matches `(int)(long)(char)expr` and subs it for `(int)(char)expr`
class TripleCStyleCastElimRule : public InferenceRule {
 public:
  TripleCStyleCastElimRule()
      : InferenceRule(cStyleCastExpr(
            stmt().bind("cast"), hasType(isInteger()),
            has(ignoringParenImpCasts(cStyleCastExpr(
                hasType(isInteger()), has(ignoringImpCasts(cStyleCastExpr(
                                          hasType(isInteger()))))))))) {}

  void run(const MatchFinder::MatchResult &result) {
    match = result.Nodes.getNodeAs<clang::CStyleCastExpr>("cast");
  }

  clang::Stmt *GetOrCreateSubstitution(StmtToIRMap &provenance,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) {
    auto cast{clang::cast<clang::CStyleCastExpr>(stmt)};
    CHECK(cast == match)
        << "Substituted CStyleCastExpr is not the matched CStyleCastExpr!";

    auto &ctx{unit.getASTContext()};

    auto subcast{clang::cast<clang::CStyleCastExpr>(
        cast->getSubExpr()->IgnoreParenImpCasts())};

    auto subsubcast{clang::cast<clang::CStyleCastExpr>(
        subcast->getSubExpr()->IgnoreParenImpCasts())};

    if (ctx.getTypeSize(cast->getType()) ==
            ctx.getTypeSize(subsubcast->getType()) &&
        ctx.getTypeSize(cast->getType()) <=
            ctx.getTypeSize(subcast->getType())) {
      return ASTBuilder(unit).CreateCStyleCast(cast->getType(), subsubcast);
    }

    return cast;
  }
};

// Matches (type *)0U and subs it for 0U
class CStyleZeroToPtrCastElimRule : public InferenceRule {
 public:
  CStyleZeroToPtrCastElimRule()
      : InferenceRule(
            cStyleCastExpr(hasType(pointerType()),
                           has(ignoringImpCasts(integerLiteral(equals(0U)))))
                .bind("cast")) {}

  void run(const MatchFinder::MatchResult &result) {
    match = result.Nodes.getNodeAs<clang::CStyleCastExpr>("cast");
  }

  clang::Stmt *GetOrCreateSubstitution(StmtToIRMap &provenance,
                                       clang::ASTUnit &unit,
                                       clang::Stmt *stmt) {
    auto cast{clang::cast<clang::CStyleCastExpr>(stmt)};
    CHECK(cast == match)
        << "Substituted CStyleCastExpr is not the matched CStyleCastExpr!";

    return cast->getSubExpr()->IgnoreParenImpCasts();
  }
};

}  // namespace

char ExprCombine::ID = 0;

ExprCombine::ExprCombine(StmtToIRMap &provenance, clang::ASTUnit &u)
    : ModulePass(ExprCombine::ID),
      TransformVisitor<ExprCombine>(provenance),
      unit(u) {}

bool ExprCombine::VisitCStyleCastExpr(clang::CStyleCastExpr *cast) {
  // TODO(frabert): Re-enable nullptr casts simplification

  clang::Expr::EvalResult result;
  auto &ctx{unit.getASTContext()};
  if (cast->EvaluateAsRValue(result, ctx)) {
    if (result.HasSideEffects || result.HasUndefinedBehavior) {
      return true;
    }

    switch (result.Val.getKind()) {
      case clang::APValue::ValueKind::Int: {
        auto sub{ASTBuilder(unit).CreateAdjustedIntLit(result.Val.getInt())};
        if (GetHash(ctx, cast) != GetHash(ctx, sub)) {
          substitutions[cast] = sub;
        }
      } break;

      default:
        break;
    }

    return true;
  }

  std::vector<std::unique_ptr<InferenceRule>> rules;

  rules.emplace_back(new UnsignedToSignedCStyleCastRule);
  rules.emplace_back(new TripleCStyleCastElimRule);

  auto sub{ApplyFirstMatchingRule(provenance, unit, cast, rules)};
  if (sub != cast) {
    substitutions[cast] = sub;
  }

  return true;
}

bool ExprCombine::VisitUnaryOperator(clang::UnaryOperator *op) {
  // DLOG(INFO) << "VisitUnaryOperator: "
  //            << op->getOpcodeStr(op->getOpcode()).str();
  std::vector<std::unique_ptr<InferenceRule>> rules;

  rules.emplace_back(new NegComparisonRule);
  rules.emplace_back(new DerefAddrOfRule);
  rules.emplace_back(new AddrOfArraySubscriptRule);

  auto sub{ApplyFirstMatchingRule(provenance, unit, op, rules)};
  if (sub != op) {
    substitutions[op] = sub;
  }

  return true;
}

bool ExprCombine::VisitBinaryOperator(clang::BinaryOperator *op) {
  // DLOG(INFO) << "VisitBinaryOperator: " << op->getOpcodeStr().str();
  std::vector<std::unique_ptr<InferenceRule>> rules;

  rules.emplace_back(new AssignCastedExprRule);

  auto sub{ApplyFirstMatchingRule(provenance, unit, op, rules)};
  if (sub != op) {
    substitutions[op] = sub;
  }

  return true;
}

bool ExprCombine::VisitArraySubscriptExpr(clang::ArraySubscriptExpr *expr) {
  // DLOG(INFO) << "VisitArraySubscriptExpr";
  std::vector<std::unique_ptr<InferenceRule>> rules;

  rules.emplace_back(new ArraySubscriptAddrOfRule);

  auto sub{ApplyFirstMatchingRule(provenance, unit, expr, rules)};
  if (sub != expr) {
    substitutions[expr] = sub;
  }

  return true;
}

bool ExprCombine::VisitMemberExpr(clang::MemberExpr *expr) {
  // DLOG(INFO) << "VisitMemberExpr";
  std::vector<std::unique_ptr<InferenceRule>> rules;

  rules.emplace_back(new MemberExprAddrOfRule);
  rules.emplace_back(new MemberExprArraySubRule);

  auto sub{ApplyFirstMatchingRule(provenance, unit, expr, rules)};
  if (sub != expr) {
    substitutions[expr] = sub;
  }

  return true;
}

bool ExprCombine::VisitParenExpr(clang::ParenExpr *expr) {
  // DLOG(INFO) << "VisitParenExpr";
  std::vector<std::unique_ptr<InferenceRule>> rules;

  rules.emplace_back(new ParenDeclRefExprStripRule);

  auto sub{ApplyFirstMatchingRule(provenance, unit, expr, rules)};
  if (sub != expr) {
    substitutions[expr] = sub;
  }

  return true;
}

bool ExprCombine::runOnModule(llvm::Module &module) {
  LOG(INFO) << "Rule-based statement simplification";
  Initialize();
  TraverseDecl(unit.getASTContext().getTranslationUnitDecl());
  return changed;
}

}  // namespace rellic