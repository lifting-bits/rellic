/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/InlineReferences.h"

#include <clang/AST/Decl.h>
#include <clang/AST/Expr.h>
#include <clang/AST/OperationKinds.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/StmtVisitor.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include <unordered_map>
#include <vector>

#include "rellic/AST/ASTBuilder.h"
#include "rellic/AST/DecompilationContext.h"
#include "rellic/AST/TransformVisitor.h"
#include "rellic/AST/Util.h"

namespace rellic {
InlineReferences::InlineReferences(DecompilationContext& dec_ctx)
    : TransformVisitor<InlineReferences>(dec_ctx) {}

class ReferenceCounter : public clang::RecursiveASTVisitor<ReferenceCounter> {
 private:
  DecompilationContext& dec_ctx;

 public:
  std::unordered_map<llvm::Value*, unsigned>& referenced_values;

 private:
  void GetReferencedValues(z3::expr expr) {
    if (expr.decl().decl_kind() == Z3_OP_EQ) {
      // Equalities generated form the reaching conditions of switch
      // instructions Always in the form (VAR == CONST) or (CONST == VAR) VAR
      // will uniquely identify a SwitchInst, CONST will represent the index of
      // the case taken
      CHECK_EQ(expr.num_args(), 2) << "Equalities must have 2 arguments";
      auto a{expr.arg(0)};
      auto b{expr.arg(1)};

      llvm::SwitchInst* inst{dec_ctx.z3_sw_vars_inv[a.id()]};
      unsigned case_idx{};

      // GenerateAST always generates equalities in the form (VAR == CONST), but
      // there is a chance that some Z3 simplification inverts the order, so
      // handle that here.
      if (!inst) {
        inst = dec_ctx.z3_sw_vars_inv[b.id()];
        case_idx = a.get_numeral_uint();
      } else {
        case_idx = b.get_numeral_uint();
      }

      for (auto sw_case : inst->cases()) {
        if (sw_case.getCaseIndex() == case_idx) {
          ++referenced_values[inst->getOperandUse(0)];
          return;
        }
      }

      LOG(FATAL) << "Couldn't find switch case";
      return;
    }

    auto hash{expr.id()};
    if (dec_ctx.z3_br_edges_inv.find(hash) != dec_ctx.z3_br_edges_inv.end()) {
      auto edge{dec_ctx.z3_br_edges_inv[hash]};
      CHECK(edge.second) << "Inverse map should only be populated for branches "
                            "taken when condition is true";
      // expr is a variable that represents the condition of a branch
      // instruction.

      // FIXME(frabert): Unfortunately there is no public API in BranchInst that
      // gives the operand of the condition. From reverse engineering LLVM code,
      // this is the way they obtain uses internally, but it's probably not
      // stable.
      ++referenced_values[*(edge.first->op_end() - 3)];
      return;
    }

    switch (expr.decl().decl_kind()) {
      case Z3_OP_TRUE:
      case Z3_OP_FALSE:
        CHECK_EQ(expr.num_args(), 0) << "Literals cannot have arguments";
        return;
      case Z3_OP_AND:
      case Z3_OP_OR: {
        for (auto i{0U}; i < expr.num_args(); ++i) {
          GetReferencedValues(expr.arg(i));
        }
        return;
      }
      case Z3_OP_NOT: {
        CHECK_EQ(expr.num_args(), 1) << "Not must have one argument";
        GetReferencedValues(expr.arg(0));
        return;
      }
      default:
        LOG(FATAL) << "Invalid z3 op";
    }
  }

 public:
  ReferenceCounter(DecompilationContext& dec_ctx,
                   std::unordered_map<llvm::Value*, unsigned>& refs)
      : dec_ctx(dec_ctx), referenced_values(refs) {}

  template <typename T>
  void VisitConditionedStmt(T* stmt) {
    if (stmt->getCond() == dec_ctx.marker_expr) {
      auto cond{dec_ctx.z3_exprs[dec_ctx.conds[stmt]]};
      GetReferencedValues(cond);
    }
  }

  bool VisitIfStmt(clang::IfStmt* stmt) {
    VisitConditionedStmt(stmt);
    return true;
  }

  bool VisitWhileStmt(clang::WhileStmt* stmt) {
    VisitConditionedStmt(stmt);
    return true;
  }

  bool VisitDoStmt(clang::DoStmt* stmt) {
    VisitConditionedStmt(stmt);
    return true;
  }
};

void InlineReferences::RunImpl() {
  LOG(INFO) << "Inlining references";
  TransformVisitor<InlineReferences>::RunImpl();
  changed = false;
  refs.clear();
  removable_decls.clear();
  ReferenceCounter counter{dec_ctx, refs};

  for (auto decl : dec_ctx.ast_ctx.getTranslationUnitDecl()->decls()) {
    if (auto fdecl = clang::dyn_cast<clang::FunctionDecl>(decl)) {
      if (Stopped()) {
        return;
      }

      if (fdecl->hasBody()) {
        counter.TraverseStmt(fdecl->getBody());
      }
    }
  }

  for (auto& [value, count_refs] : refs) {
    if (count_refs > 2) {
      continue;
    }
    auto it{dec_ctx.value_decls.find(value)};
    if (it == dec_ctx.value_decls.end()) {
      continue;
    }
    removable_decls.insert(it->second);
    dec_ctx.value_decls.erase(it);
  }

  TraverseDecl(dec_ctx.ast_ctx.getTranslationUnitDecl());
}

bool InlineReferences::VisitCompoundStmt(clang::CompoundStmt* stmt) {
  std::vector<clang::Stmt*> new_body;
  bool should_substitute{false};
  for (auto child : stmt->body()) {
    if (Stopped()) {
      break;
    }

    bool add_to_new_body{true};
    do {
      auto bop{clang::dyn_cast<clang::BinaryOperator>(child)};
      if (!bop) {
        break;
      }

      if (bop->getOpcode() != clang::BO_Assign) {
        LOG(INFO) << ClangThingToString(bop) << " not an assignment";
        break;
      }

      auto declref{clang::dyn_cast<clang::DeclRefExpr>(bop->getLHS())};
      if (!declref) {
        LOG(INFO) << ClangThingToString(bop->getLHS()) << " not a declref";
        break;
      }

      if (!removable_decls.count(declref->getDecl())) {
        LOG(INFO) << ClangThingToString(declref->getDecl())
                  << " not a removable decl";
        break;
      }

      add_to_new_body = false;
    } while (false);

    do {
      auto declstmt{clang::dyn_cast<clang::DeclStmt>(child)};
      if (!declstmt) {
        break;
      }

      auto vardecl{clang::dyn_cast<clang::VarDecl>(declstmt->getSingleDecl())};
      if (!vardecl) {
        break;
      }

      if (!removable_decls.count(vardecl)) {
        break;
      }

      add_to_new_body = false;
    } while (false);

    if (add_to_new_body) {
      new_body.push_back(child);
    } else {
      should_substitute = true;
    }
  }

  if (!Stopped() && should_substitute) {
    substitutions[stmt] = dec_ctx.ast.CreateCompoundStmt(new_body);
  }
  return !Stopped();
}
}  // namespace rellic