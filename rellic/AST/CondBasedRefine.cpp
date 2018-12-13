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

#include "rellic/AST/CondBasedRefine.h"

namespace rellic {

namespace {

using IfStmtSet = std::set<clang::IfStmt *>;

static IfStmtSet GetIfStmts(clang::CompoundStmt *compound) {
  IfStmtSet result;
  for (auto stmt : compound->body()) {
    if (auto ifstmt = llvm::dyn_cast<clang::IfStmt>(stmt)) {
      result.insert(ifstmt);
    }
  }
  return result;
}

static void SplitClause(z3::expr expr, z3::expr_vector &clauses) {
  if (expr.decl().decl_kind() == Z3_OP_AND) {
    // Make sure we have a flat n-ary `and`
    if (expr.num_args() == 2) {
      expr = expr.simplify();
    }
    for (unsigned i = 0; i < expr.num_args(); ++i) {
      clauses.push_back(expr.arg(i));
    }
  } else {
    clauses.push_back(expr);
  }
}

}  // namespace

char CondBasedRefine::ID = 0;

CondBasedRefine::CondBasedRefine(clang::CompilerInstance &ins,
                                 rellic::IRToASTVisitor &ast_gen)
    : ModulePass(CondBasedRefine::ID),
      ast_ctx(&ins.getASTContext()),
      ast_gen(&ast_gen),
      z3_ctx(new z3::context()),
      z3_gen(new rellic::Z3ConvVisitor(ast_ctx, z3_ctx.get())) {}

bool CondBasedRefine::ThenTest(z3::expr lhs, z3::expr rhs) {
  auto Pred = [](z3::expr a, z3::expr b) {
    auto test = (!a && b).simplify();
    return test.bool_value() != Z3_L_FALSE;
  };

  z3::expr_vector lhs_c(*z3_ctx), rhs_c(*z3_ctx);
  SplitClause(lhs, lhs_c);
  SplitClause(rhs, rhs_c);

  for (unsigned i = 0; i < lhs_c.size(); ++i) {
    for (unsigned j = 0; j < rhs_c.size(); ++j) {
      if (!Pred(lhs_c[i], rhs_c[j])) {
        return true;
      }
    }
  }

  return false;
}

bool CondBasedRefine::ElseTest(z3::expr lhs, z3::expr rhs) {
  auto Pred = [](z3::expr a, z3::expr b) {
    auto test = (a || b).simplify();
    return test.bool_value() != Z3_L_TRUE;
  };

  z3::expr_vector lhs_c(*z3_ctx), rhs_c(*z3_ctx);
  SplitClause(lhs, lhs_c);
  SplitClause(rhs, rhs_c);

  for (unsigned i = 0; i < lhs_c.size(); ++i) {
    for (unsigned j = 0; j < rhs_c.size(); ++j) {
      if (!Pred(lhs_c[i], rhs_c[j])) {
        return true;
      }
    }
  }

  return false;
}

z3::expr CondBasedRefine::GetZ3Cond(clang::IfStmt *ifstmt) {
  auto cond = ifstmt->getCond();
  auto expr = z3_gen->Z3BoolCast(z3_gen->GetOrCreateZ3Expr(cond));
  return expr.simplify();
}

void CondBasedRefine::CreateIfThenElseStmts(IfStmtSet worklist) {
  while (!worklist.empty()) {
    auto lhs = *worklist.begin();
    worklist.erase(lhs);
    // Prepare conditions according to which we're going to
    // cluster statements. First according to a the whole `lhs`
    // condition. Then according to it's `&&` subconditions clauses.
    z3::expr_vector clauses(*z3_ctx);
    clauses.push_back(GetZ3Cond(lhs));
    SplitClause(clauses[0], clauses);
    // This is where the magic happens
    for (unsigned i = 0; i < clauses.size(); ++i) {
      auto clause = clauses[i];
      // Get branch candidates wrt `clause`
      std::vector<clang::Stmt *> thens({lhs}), elses;
      for (auto rhs : worklist) {
        auto rcond = GetZ3Cond(rhs);
        if (ThenTest(clause, rcond)) {
          thens.push_back(rhs);
        } else if (ElseTest(clause, rcond)) {
          elses.push_back(rhs);
        }
      }
      // Create an if-then-else if possible
      if (thens.size() + elses.size() > 1) {
        // Erase then statements from the AST and `worklist`
        for (auto stmt : thens) {
          worklist.erase(llvm::cast<clang::IfStmt>(stmt));
          substitutions[stmt] = nullptr;
        }
        // Create our new if-then
        auto sub = CreateIfStmt(*ast_ctx, z3_gen->GetOrCreateCExpr(clause),
                                CreateCompoundStmt(*ast_ctx, thens));
        // Create an else branch if possible
        if (!elses.empty()) {
          // Erase else statements from the AST and `worklist`
          for (auto stmt : elses) {
            worklist.erase(llvm::cast<clang::IfStmt>(stmt));
            substitutions[stmt] = nullptr;
          }
          // Add the else branch
          sub->setElse(CreateCompoundStmt(*ast_ctx, elses));
        }
        // Replace `lhs` with the new `sub`
        substitutions[lhs] = sub;
      }
    }
  }
}

bool CondBasedRefine::VisitCompoundStmt(clang::CompoundStmt *compound) {
  // DLOG(INFO) << "VisitCompoundStmt";
  // Create if-then-else substitutions for IfStmts in `compound`
  CreateIfThenElseStmts(GetIfStmts(compound));
  // Apply created if-then-else substitutions and
  // create a replacement for `compound`
  if (ReplaceChildren(compound, substitutions)) {
    std::vector<clang::Stmt *> new_body;
    for (auto stmt : compound->body()) {
      if (stmt) {
        new_body.push_back(stmt);
      }
    }
    substitutions[compound] = CreateCompoundStmt(*ast_ctx, new_body);
  }
  return true;
}

bool CondBasedRefine::runOnModule(llvm::Module &module) {
  LOG(INFO) << "Condition-based refinement";
  Initialize();
  TraverseDecl(ast_ctx->getTranslationUnitDecl());
  return changed;
}

llvm::ModulePass *createCondBasedRefinePass(clang::CompilerInstance &ins,
                                            rellic::IRToASTVisitor &gen) {
  return new CondBasedRefine(ins, gen);
}
}  // namespace rellic