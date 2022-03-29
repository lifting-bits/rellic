/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "rellic/AST/BlockVisitor.h"

#define GOOGLE_STRIP_LOG 1
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/BC/Util.h"

namespace rellic {
BlockVisitor::BlockVisitor(clang::ASTContext &ast_ctx, ASTBuilder &ast,
                           IRToASTVisitor &ast_gen,
                           std::vector<clang::Stmt *> &stmts,
                           Provenance &provenance)
    : ast_ctx(ast_ctx),
      ast(ast),
      ast_gen(ast_gen),
      stmts(stmts),
      provenance(provenance) {}

clang::Stmt *BlockVisitor::visitStoreInst(llvm::StoreInst &inst) {
  DLOG(INFO) << "visitStoreInst: " << LLVMThingToString(&inst);
  // Stores in LLVM IR correspond to value assignments in C
  // Get the operand we're assigning to
  auto lhs{ast_gen.GetOperandExpr(
      inst.getOperandUse(inst.getPointerOperandIndex()))};
  // Get the operand we're assigning from
  auto &value_opnd{inst.getOperandUse(0)};
  if (auto undef = llvm::dyn_cast<llvm::UndefValue>(value_opnd)) {
    DLOG(INFO) << "Invalid store ignored: " << LLVMThingToString(&inst);
    return nullptr;
  }
  auto rhs{ast_gen.GetOperandExpr(value_opnd)};
  if (value_opnd->getType()->isArrayTy()) {
    // We cannot directly assign arrays, so we generate memcpy or memset
    // instead
    auto DL{inst.getModule()->getDataLayout()};
    llvm::APInt sz(64, DL.getTypeAllocSize(value_opnd->getType()));
    auto sz_expr{ast.CreateIntLit(sz)};
    if (llvm::isa<llvm::ConstantAggregateZero>(value_opnd)) {
      llvm::APInt zero(8, 0, false);
      std::vector<clang::Expr *> args{lhs, ast.CreateIntLit(zero), sz_expr};
      return ast.CreateBuiltinCall(clang::Builtin::BI__builtin_memset, args);
    } else {
      std::vector<clang::Expr *> args{lhs, rhs, sz_expr};
      return ast.CreateBuiltinCall(clang::Builtin::BI__builtin_memcpy, args);
    }
  } else {
    // Create the assignemnt itself
    auto deref{ast.CreateDeref(lhs)};
    CopyProvenance(lhs, deref, provenance.use_provenance);
    return ast.CreateAssign(deref, rhs);
  }
}

clang::Stmt *BlockVisitor::visitCallInst(llvm::CallInst &inst) {
  auto &var{provenance.value_decls[&inst]};
  auto expr{ast_gen.visit(inst)};
  if (var) {
    return ast.CreateAssign(ast.CreateDeclRef(var), expr);
  }
  return expr;
}

clang::Stmt *BlockVisitor::visitReturnInst(llvm::ReturnInst &inst) {
  DLOG(INFO) << "visitReturnInst: " << LLVMThingToString(&inst);
  if (auto retval = inst.getReturnValue()) {
    return ast.CreateReturn(ast_gen.GetOperandExpr(inst.getOperandUse(0)));
  } else {
    return ast.CreateReturn();
  }
}

clang::Stmt *BlockVisitor::visitBranchInst(llvm::BranchInst &inst) {
  DLOG(INFO) << "visitBranchInst ignored: " << LLVMThingToString(&inst);
  return nullptr;
}

clang::Stmt *BlockVisitor::visitUnreachableInst(llvm::UnreachableInst &inst) {
  DLOG(INFO) << "visitUnreachableInst ignored:" << LLVMThingToString(&inst);
  return nullptr;
}

clang::Stmt *BlockVisitor::visitInstruction(llvm::Instruction &inst) {
  auto &var{provenance.value_decls[&inst]};
  if (var) {
    auto expr{ast_gen.visit(inst)};
    return ast.CreateAssign(ast.CreateDeclRef(var), expr);
  }
  return nullptr;
}

void BlockVisitor::visitBasicBlock(llvm::BasicBlock &block) {
  for (auto &inst : block) {
    auto res{visit(inst)};
    if (res) {
      provenance.stmt_provenance[res] = &inst;
      stmts.push_back(res);
    }
  }
}
}  // namespace rellic