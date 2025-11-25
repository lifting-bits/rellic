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
#include "rellic/AST/Util.h"

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

  void run(const MatchFinder::MatchResult &result) override {
    auto op = result.Nodes.getNodeAs<clang::UnaryOperator>("base");
    if (op->getOpcode() == clang::UO_AddrOf) {
      match = result.Nodes.getNodeAs<clang::ArraySubscriptExpr>("sub");
    }
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::Stmt *stmt) override {
    auto sub = clang::cast<clang::ArraySubscriptExpr>(stmt);
    CHECK(sub == match) << "Substituted ArraySubscriptExpr is not the matched "
                           "ArraySubscriptExpr!";
    auto paren = clang::cast<clang::ParenExpr>(sub->getBase());
    auto addr_of = clang::cast<clang::UnaryOperator>(paren->getSubExpr());
    CopyProvenance(addr_of, addr_of->getSubExpr(), dec_ctx.use_provenance);
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

  void run(const MatchFinder::MatchResult &result) override {
    match = result.Nodes.getNodeAs<clang::UnaryOperator>("addr_of");
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::Stmt *stmt) override {
    auto addr_of = clang::cast<clang::UnaryOperator>(stmt);
    CHECK(addr_of == match)
        << "Substituted UnaryOperator is not the matched UnaryOperator!";
    auto subexpr = addr_of->getSubExpr()->IgnoreParenImpCasts();
    auto sub = clang::cast<clang::ArraySubscriptExpr>(subexpr);
    CopyProvenance(sub, sub->getBase(), dec_ctx.use_provenance);
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

  void run(const MatchFinder::MatchResult &result) override {
    match = result.Nodes.getNodeAs<clang::UnaryOperator>("deref");
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::Stmt *stmt) override {
    auto deref = clang::cast<clang::UnaryOperator>(stmt);
    CHECK(deref == match)
        << "Substituted UnaryOperator is not the matched UnaryOperator!";
    auto subexpr = deref->getSubExpr()->IgnoreParenImpCasts();
    auto addr_of = clang::cast<clang::UnaryOperator>(subexpr);
    CopyProvenance(addr_of, addr_of->getSubExpr(), dec_ctx.use_provenance);
    return addr_of->getSubExpr();
  }
};

// Matches `*(cond ? &expr1 : &expr2)` and subs it for `cond ? expr1 : expr2`
class DerefAddrOfConditionalRule : public InferenceRule {
 public:
  DerefAddrOfConditionalRule()
      : InferenceRule(unaryOperator(
            stmt().bind("deref"), hasOperatorName("*"),
            has(ignoringParenImpCasts(conditionalOperator(
                hasTrueExpression(
                    ignoringParenImpCasts(unaryOperator(hasOperatorName("&")))),
                hasFalseExpression(ignoringParenImpCasts(
                    unaryOperator(hasOperatorName("&"))))))))) {}

  void run(const MatchFinder::MatchResult &result) override {
    match = result.Nodes.getNodeAs<clang::UnaryOperator>("deref");
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::Stmt *stmt) override {
    auto deref = clang::cast<clang::UnaryOperator>(stmt);
    CHECK(deref == match)
        << "Substituted UnaryOperator is not the matched UnaryOperator!";

    if (deref->getValueKind() == clang::ExprValueKind::VK_LValue) {
      return deref;
    }

    auto subexpr = deref->getSubExpr()->IgnoreParenImpCasts();
    auto conditional = clang::cast<clang::ConditionalOperator>(subexpr);
    auto addr_of1 =
        clang::cast<clang::UnaryOperator>(conditional->getTrueExpr());
    auto addr_of2 =
        clang::cast<clang::UnaryOperator>(conditional->getFalseExpr());
    CopyProvenance(addr_of1, addr_of1->getSubExpr(), dec_ctx.use_provenance);
    CopyProvenance(addr_of2, addr_of2->getSubExpr(), dec_ctx.use_provenance);

    return dec_ctx.ast.CreateConditional(
        conditional->getCond(), addr_of1->getSubExpr(), addr_of2->getSubExpr());
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

  void run(const MatchFinder::MatchResult &result) override {
    auto binop = result.Nodes.getNodeAs<clang::BinaryOperator>("binop");
    if (binop->isComparisonOp()) {
      match = result.Nodes.getNodeAs<clang::UnaryOperator>("not");
    }
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::Stmt *stmt) override {
    auto op = clang::cast<clang::UnaryOperator>(stmt);
    CHECK(op == match)
        << "Substituted UnaryOperator is not the matched UnaryOperator!";
    auto subexpr = op->getSubExpr()->IgnoreParenImpCasts();
    auto binop = clang::cast<clang::BinaryOperator>(subexpr);
    auto opc = clang::BinaryOperator::negateComparisonOp(binop->getOpcode());
    auto res =
        dec_ctx.ast.CreateBinaryOp(opc, binop->getLHS(), binop->getRHS());
    CopyProvenance(op, res, dec_ctx.stmt_provenance);
    return res;
  }
};

// Matches `(a)` and subs it for `a`
class ParenDeclRefExprStripRule : public InferenceRule {
 public:
  ParenDeclRefExprStripRule()
      : InferenceRule(
            parenExpr(stmt().bind("paren"),
                      has(ignoringImpCasts(declRefExpr(to(varDecl())))))) {}

  void run(const MatchFinder::MatchResult &result) override {
    match = result.Nodes.getNodeAs<clang::ParenExpr>("paren");
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::Stmt *stmt) override {
    auto paren = clang::cast<clang::ParenExpr>(stmt);
    CHECK(paren == match)
        << "Substituted ParenExpr is not the matched ParenExpr!";
    return paren->getSubExpr();
  }
};

// Matches `((expr))` and subs it for `(expr)`
class DoubleParenStripRule : public InferenceRule {
 public:
  DoubleParenStripRule()
      : InferenceRule(parenExpr(stmt().bind("paren"), has(parenExpr()))) {}

  void run(const MatchFinder::MatchResult &result) override {
    match = result.Nodes.getNodeAs<clang::ParenExpr>("paren");
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::Stmt *stmt) override {
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

  void run(const MatchFinder::MatchResult &result) override {
    auto arrow{result.Nodes.getNodeAs<clang::MemberExpr>("arrow")};
    if (result.Nodes.getNodeAs<clang::Expr>("base") == arrow->getBase()) {
      match = arrow;
    }
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::Stmt *stmt) override {
    auto arrow{clang::cast<clang::MemberExpr>(stmt)};
    CHECK(arrow == match)
        << "Substituted MemberExpr is not the matched MemberExpr!";
    auto base{arrow->getBase()->IgnoreParenImpCasts()};
    auto addr_of{clang::cast<clang::UnaryOperator>(base)};
    auto field{clang::dyn_cast<clang::FieldDecl>(arrow->getMemberDecl())};
    CHECK(field != nullptr)
        << "Substituted MemberExpr is not a structure field access!";
    CopyProvenance(addr_of, addr_of->getSubExpr(), dec_ctx.use_provenance);
    return dec_ctx.ast.CreateDot(addr_of->getSubExpr(), field);
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

  void run(const MatchFinder::MatchResult &result) override {
    auto dot{result.Nodes.getNodeAs<clang::MemberExpr>("dot")};
    if (result.Nodes.getNodeAs<clang::Expr>("base") == dot->getBase()) {
      match = dot;
    }
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::Stmt *stmt) override {
    auto dot{clang::cast<clang::MemberExpr>(stmt)};
    CHECK(dot == match)
        << "Substituted MemberExpr is not the matched MemberExpr!";
    auto base{dot->getBase()->IgnoreParenImpCasts()};
    auto sub{clang::cast<clang::ArraySubscriptExpr>(base)};
    auto field{clang::dyn_cast<clang::FieldDecl>(dot->getMemberDecl())};
    CHECK(field != nullptr)
        << "Substituted MemberExpr is not a structure field access!";
    CopyProvenance(sub, sub->getBase(), dec_ctx.use_provenance);
    return dec_ctx.ast.CreateArrow(sub->getBase(), field);
  }
};

// Matches `a = (type)expr`, where `a` is of `type` and subs it for `a = expr`
class AssignCastedExprRule : public InferenceRule {
 public:
  AssignCastedExprRule()
      : InferenceRule(binaryOperator(
            stmt().bind("assign"), hasOperatorName("="),
            has(ignoringParenImpCasts(cStyleCastExpr(stmt().bind("cast")))))) {}

  void run(const MatchFinder::MatchResult &result) override {
    auto assign{result.Nodes.getNodeAs<clang::BinaryOperator>("assign")};
    auto cast{result.Nodes.getNodeAs<clang::CStyleCastExpr>("cast")};
    if (assign->getType() == cast->getType()) {
      match = assign;
    };
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::Stmt *stmt) override {
    auto assign{clang::cast<clang::BinaryOperator>(stmt)};
    CHECK(assign == match)
        << "Substituted BinaryOperator is not the matched BinaryOperator!";

    auto lhs{assign->getLHS()};

    auto rhs{clang::cast<clang::CStyleCastExpr>(
        assign->getRHS()->IgnoreParenImpCasts())};

    if (dec_ctx.ast_unit.getSema().CheckAssignmentConstraints(
            clang::SourceLocation(), lhs->getType(),
            rhs->getSubExpr()->getType()) ==
        clang::Sema::AssignConvertType::Compatible) {
      return dec_ctx.ast.CreateAssign(lhs, rhs->getSubExpr());
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

  void run(const MatchFinder::MatchResult &result) override {
    match = result.Nodes.getNodeAs<clang::CStyleCastExpr>("cast");
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::Stmt *stmt) override {
    auto cast{clang::cast<clang::CStyleCastExpr>(stmt)};
    CHECK(cast == match)
        << "Substituted CStyleCastExpr is not the matched CStyleCastExpr!";

    auto subcast{clang::cast<clang::CStyleCastExpr>(
        cast->getSubExpr()->IgnoreParenImpCasts())};

    if (dec_ctx.ast_unit.getASTContext().getCorrespondingUnsignedType(
            cast->getType()) == subcast->getType()) {
      return dec_ctx.ast.CreateCStyleCast(cast->getType(),
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

  void run(const MatchFinder::MatchResult &result) override {
    match = result.Nodes.getNodeAs<clang::CStyleCastExpr>("cast");
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::Stmt *stmt) override {
    auto cast{clang::cast<clang::CStyleCastExpr>(stmt)};
    CHECK(cast == match)
        << "Substituted CStyleCastExpr is not the matched CStyleCastExpr!";

    auto &ctx{dec_ctx.ast_ctx};

    auto subcast{clang::cast<clang::CStyleCastExpr>(
        cast->getSubExpr()->IgnoreParenImpCasts())};

    auto subsubcast{clang::cast<clang::CStyleCastExpr>(
        subcast->getSubExpr()->IgnoreParenImpCasts())};

    if (ctx.getTypeSize(cast->getType()) ==
            ctx.getTypeSize(subsubcast->getType()) &&
        ctx.getTypeSize(cast->getType()) <=
            ctx.getTypeSize(subcast->getType())) {
      return dec_ctx.ast.CreateCStyleCast(cast->getType(), subsubcast);
    }

    return cast;
  }
};

// Matches `(type *)(void *)expr` and subs it for `(type *)expr`
class VoidToTypePtrCastElimRule : public InferenceRule {
 public:
  VoidToTypePtrCastElimRule()
      : InferenceRule(
            cStyleCastExpr(stmt().bind("cast"), hasType(pointerType()),
                           has(ignoringParenImpCasts(
                               cStyleCastExpr(hasType(pointerType())))))) {}

  void run(const MatchFinder::MatchResult &result) override {
    match = result.Nodes.getNodeAs<clang::CStyleCastExpr>("cast");
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::Stmt *stmt) override {
    auto cast{clang::cast<clang::CStyleCastExpr>(stmt)};
    CHECK(cast == match)
        << "Substituted CStyleCastExpr is not the matched CStyleCastExpr!";

    auto subcast{clang::cast<clang::CStyleCastExpr>(
        cast->getSubExpr()->IgnoreParenImpCasts())};

    if (cast->getType()->isPointerType() &&
        subcast->getType()->isVoidPointerType()) {
      return dec_ctx.ast.CreateCStyleCast(
          cast->getType(), subcast->getSubExpr()->IgnoreParenImpCasts());
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

  void run(const MatchFinder::MatchResult &result) override {
    match = result.Nodes.getNodeAs<clang::CStyleCastExpr>("cast");
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::Stmt *stmt) override {
    auto cast{clang::cast<clang::CStyleCastExpr>(stmt)};
    CHECK(cast == match)
        << "Substituted CStyleCastExpr is not the matched CStyleCastExpr!";

    return cast->getSubExpr()->IgnoreParenImpCasts();
  }
};

// Matches (type)const and subs it for const directly converted to be type
class CStyleConstElimRule : public InferenceRule {
 public:
  CStyleConstElimRule()
      : InferenceRule(cStyleCastExpr(has(ignoringImpCasts(integerLiteral())))
                          .bind("cast")) {}

  void run(const MatchFinder::MatchResult &result) override {
    match = result.Nodes.getNodeAs<clang::CStyleCastExpr>("cast");
  }

  clang::Stmt *GetOrCreateSubstitution(DecompilationContext &dec_ctx,
                                       clang::Stmt *stmt) override {
    auto cast{clang::cast<clang::CStyleCastExpr>(stmt)};
    CHECK(cast == match)
        << "Substituted CStyleCastExpr is not the matched CStyleCastExpr!";

    auto int_lit{clang::cast<clang::IntegerLiteral>(
        cast->getSubExpr()->IgnoreParenImpCasts())};
    auto type{cast->getType()};
    auto size{dec_ctx.ast_unit.getASTContext().getTypeSize(type)};
    auto value{int_lit->getValue().trunc(size)};
    return dec_ctx.ast.CreateIntLit(
        llvm::APSInt(value, !type->isSignedIntegerType()));
  }
};

}  // namespace

ExprCombine::ExprCombine(DecompilationContext &dec_ctx)
    : TransformVisitor<ExprCombine>(dec_ctx) {}

bool ExprCombine::VisitCStyleCastExpr(clang::CStyleCastExpr *cast) {
  // TODO(frabert): Re-enable nullptr casts simplification

  if (cast->getCastKind() == clang::CastKind::CK_NoOp) {
    substitutions[cast] = cast->getSubExpr();
    return true;
  }

  std::vector<std::unique_ptr<InferenceRule>> pre_rules;

  pre_rules.emplace_back(new VoidToTypePtrCastElimRule);

  auto pre_sub{ApplyFirstMatchingRule(dec_ctx, cast, pre_rules)};
  if (pre_sub != cast) {
    substitutions[cast] = pre_sub;
    return true;
  }

  clang::Expr::EvalResult result;
  if (cast->EvaluateAsRValue(result, dec_ctx.ast_ctx)) {
    if (result.HasSideEffects || result.HasUndefinedBehavior) {
      return true;
    }

    switch (result.Val.getKind()) {
      case clang::APValue::ValueKind::Int: {
        auto sub{dec_ctx.ast.CreateIntLit(result.Val.getInt())};
        if (GetHash(dec_ctx.ast_ctx, cast) != GetHash(dec_ctx.ast_ctx, sub)) {
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
  rules.emplace_back(new CStyleConstElimRule);

  auto sub{ApplyFirstMatchingRule(dec_ctx, cast, rules)};
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
  rules.emplace_back(new DerefAddrOfConditionalRule);
  rules.emplace_back(new AddrOfArraySubscriptRule);

  auto sub{ApplyFirstMatchingRule(dec_ctx, op, rules)};
  if (sub != op) {
    substitutions[op] = sub;
  }

  return true;
}

bool ExprCombine::VisitBinaryOperator(clang::BinaryOperator *op) {
  // DLOG(INFO) << "VisitBinaryOperator: " << op->getOpcodeStr().str();
  std::vector<std::unique_ptr<InferenceRule>> rules;

  rules.emplace_back(new AssignCastedExprRule);

  auto sub{ApplyFirstMatchingRule(dec_ctx, op, rules)};
  if (sub != op) {
    substitutions[op] = sub;
  }

  return true;
}

bool ExprCombine::VisitArraySubscriptExpr(clang::ArraySubscriptExpr *expr) {
  // DLOG(INFO) << "VisitArraySubscriptExpr";
  std::vector<std::unique_ptr<InferenceRule>> rules;

  rules.emplace_back(new ArraySubscriptAddrOfRule);

  auto sub{ApplyFirstMatchingRule(dec_ctx, expr, rules)};
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

  auto sub{ApplyFirstMatchingRule(dec_ctx, expr, rules)};
  if (sub != expr) {
    substitutions[expr] = sub;
  }

  return true;
}

bool ExprCombine::VisitParenExpr(clang::ParenExpr *expr) {
  // DLOG(INFO) << "VisitParenExpr";
  std::vector<std::unique_ptr<InferenceRule>> rules;

  rules.emplace_back(new ParenDeclRefExprStripRule);
  rules.emplace_back(new DoubleParenStripRule);

  auto sub{ApplyFirstMatchingRule(dec_ctx, expr, rules)};
  if (sub != expr) {
    substitutions[expr] = sub;
  }

  return true;
}

void ExprCombine::RunImpl() {
  LOG(INFO) << "Rule-based statement simplification";
  TransformVisitor<ExprCombine>::RunImpl();
  TraverseDecl(dec_ctx.ast_ctx.getTranslationUnitDecl());
}

}  // namespace rellic