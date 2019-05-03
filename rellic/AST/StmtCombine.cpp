/*
 * Copyright (c) 2018 Trail of Bits, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/InferenceRule.h"
#include "rellic/AST/StmtCombine.h"
#include "rellic/AST/Util.h"

namespace rellic {

namespace {

using namespace clang::ast_matchers;

// Matches `(&base)[0]` and subs it for `base`
class ArraySubscriptAddrOfRule : public InferenceRule {
 public:
  ArraySubscriptAddrOfRule()
      : InferenceRule(arraySubscriptExpr(
            stmt().bind("sub"),
            hasBase(parenExpr(has(unaryOperator(stmt().bind("base"))))),
            hasIndex(integerLiteral(equals(0))))) {}

  void run(const MatchFinder::MatchResult &result) {
    auto op = result.Nodes.getNodeAs<clang::UnaryOperator>("base");
    if (op->getOpcode() == clang::UO_AddrOf) {
      match = result.Nodes.getNodeAs<clang::ArraySubscriptExpr>("sub");
    }
  }

  clang::Stmt *GetOrCreateSubstitution(clang::ASTContext &ctx,
                                       clang::Stmt *stmt) {
    auto sub = clang::cast<clang::ArraySubscriptExpr>(stmt);
    CHECK(sub == match)
        << "Substituted UnaryOperator is not the matched UnaryOperator!";
    auto paren = clang::cast<clang::ParenExpr>(sub->getBase());
    auto addr_of = clang::cast<clang::UnaryOperator>(paren->getSubExpr());
    return addr_of->getSubExpr();
  }
};

}  // namespace

char StmtCombine::ID = 0;

StmtCombine::StmtCombine(clang::ASTContext &ctx,
                         rellic::IRToASTVisitor &ast_gen)
    : ModulePass(StmtCombine::ID), ast_ctx(&ctx), ast_gen(&ast_gen) {}

bool StmtCombine::VisitArraySubscriptExpr(clang::ArraySubscriptExpr *expr) {
  // DLOG(INFO) << "VisitArraySubscriptExpr";
  std::vector<InferenceRule *> rules({new ArraySubscriptAddrOfRule});
  auto sub = ApplyFirstMatchingRule(*ast_ctx, expr, rules);
  if (sub != expr) {
    substitutions[expr] = sub;
  }

  delete rules.back();

  return true;
}

bool StmtCombine::VisitUnaryOperator(clang::UnaryOperator *op) {
  // DLOG(INFO) << "VisitUnaryOperator";

  // std::vector<InferenceRule *> rules({new ArraySubscriptAddrOfRule});

  // auto sub = ApplyFirstMatchingRule(*ast_ctx, op, rules);
  // if (sub != op) {
  //   substitutions[op] = sub;
  // }

  // delete rules.back();

  return true;
}

bool StmtCombine::VisitIfStmt(clang::IfStmt *ifstmt) {
  // DLOG(INFO) << "VisitIfStmt";
  llvm::APSInt val;
  bool is_const = ifstmt->getCond()->isIntegerConstantExpr(val, *ast_ctx);
  auto compound = clang::dyn_cast<clang::CompoundStmt>(ifstmt->getThen());
  bool is_empty = compound ? compound->body_empty() : false;
  if ((is_const && !val.getBoolValue()) || is_empty) {
    substitutions[ifstmt] = nullptr;
  }
  return true;
}

bool StmtCombine::VisitCompoundStmt(clang::CompoundStmt *compound) {
  // DLOG(INFO) << "VisitCompoundStmt";
  std::vector<clang::Stmt *> new_body;
  for (auto stmt : compound->body()) {
    // Filter out nullptr statements
    if (!stmt) {
      continue;
    }
    // Add only necessary statements
    if (auto expr = clang::dyn_cast<clang::Expr>(stmt)) {
      if (expr->HasSideEffects(*ast_ctx)) {
        new_body.push_back(stmt);
      }
    } else {
      new_body.push_back(stmt);
    }
  }
  // Create the a new compound
  if (changed || new_body.size() < compound->size()) {
    substitutions[compound] = CreateCompoundStmt(*ast_ctx, new_body);
  }
  return true;
}

bool StmtCombine::runOnModule(llvm::Module &module) {
  LOG(INFO) << "Rule-based statement simplification";
  Initialize();
  TraverseDecl(ast_ctx->getTranslationUnitDecl());
  return changed;
}

llvm::ModulePass *createStmtCombinePass(clang::ASTContext &ctx,
                                        rellic::IRToASTVisitor &gen) {
  return new StmtCombine(ctx, gen);
}
}  // namespace rellic