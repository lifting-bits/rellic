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

#define GOOGLE_STRIP_LOG 1

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/BC/Compat/Value.h"
#include "rellic/BC/Util.h"

#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/Util.h"

namespace rellic {

namespace {

static clang::QualType GetQualType(clang::ASTContext &ctx, llvm::Type *type) {
  DLOG(INFO) << "GetQualType: " << rellic::LLVMThingToString(type);
  clang::QualType result;
  switch (type->getTypeID()) {
    case llvm::Type::VoidTyID:
      result = ctx.VoidTy;
      break;

    case llvm::Type::HalfTyID:
      result = ctx.HalfTy;
      break;

    case llvm::Type::FloatTyID:
      result = ctx.FloatTy;
      break;

    case llvm::Type::DoubleTyID:
      result = ctx.DoubleTy;
      break;

    case llvm::Type::IntegerTyID: {
      auto size = type->getIntegerBitWidth();
      CHECK(size > 0) << "Integer bit width has to be greater than 0";
      result = ctx.getIntTypeForBitwidth(size, 0);
    } break;

    case llvm::Type::FunctionTyID: {
      auto func = llvm::cast<llvm::FunctionType>(type);
      auto ret = GetQualType(ctx, func->getReturnType());
      std::vector<clang::QualType> params;
      for (auto param : func->params()) {
        params.push_back(GetQualType(ctx, param));
      }
      auto epi = clang::FunctionProtoType::ExtProtoInfo();
      epi.Variadic = func->isVarArg();
      result = ctx.getFunctionType(ret, params, epi);
    } break;

    case llvm::Type::PointerTyID: {
      auto ptr = llvm::cast<llvm::PointerType>(type);
      result = ctx.getPointerType(GetQualType(ctx, ptr->getElementType()));
    } break;

    case llvm::Type::ArrayTyID: {
      auto arr = llvm::cast<llvm::ArrayType>(type);
      auto elm = GetQualType(ctx, arr->getElementType());
      result = ctx.getConstantArrayType(
          elm, llvm::APInt(32, arr->getNumElements()),
          clang::ArrayType::ArraySizeModifier::Normal, 0);
    } break;

    default:
      LOG(FATAL) << "Unknown LLVM Type";
      break;
  }

  return result;
}

static clang::Expr *CreateLiteralExpr(clang::ASTContext &ast_ctx,
                                      clang::DeclContext *decl_ctx,
                                      llvm::ConstantData *cdata) {
  DLOG(INFO) << "Creating literal Expr for "
             << rellic::LLVMThingToString(cdata);

  auto type = GetQualType(ast_ctx, cdata->getType());

  clang::Expr *result = nullptr;
  if (auto integer = llvm::dyn_cast<llvm::ConstantInt>(cdata)) {
    result = clang::IntegerLiteral::Create(ast_ctx, integer->getValue(), type,
                                           clang::SourceLocation());
  } else if (auto floating = llvm::dyn_cast<llvm::ConstantFP>(cdata)) {
    result = clang::FloatingLiteral::Create(ast_ctx, floating->getValueAPF(),
                                            /*isexact=*/true, type,
                                            clang::SourceLocation());
  } else if (auto array = llvm::dyn_cast<llvm::ConstantDataArray>(cdata)) {
    if (array->isString()) {
      result = clang::StringLiteral::Create(
          ast_ctx, array->getAsString(),
          clang::StringLiteral::StringKind::Ascii,
          /*Pascal=*/false, type, clang::SourceLocation());
    } else {
      std::vector<clang::Expr *> init_exprs;
      for (unsigned i = 0; i < array->getNumElements(); ++i) {
        auto element = array->getElementAsConstant(i);
        auto cdata = llvm::cast<llvm::ConstantData>(element);
        init_exprs.push_back(CreateLiteralExpr(ast_ctx, decl_ctx, cdata));
      }
      result = CreateInitListExpr(ast_ctx, init_exprs);
    }
  } else {
    LOG(FATAL) << "Unknown LLVM constant type";
  }

  return result;
}

static clang::VarDecl *CreateVarDecl(clang::ASTContext &ast_ctx,
                                     clang::DeclContext *decl_ctx,
                                     llvm::Type *type, std::string name) {
  DLOG(INFO) << "Creating VarDecl for " << name;
  return clang::VarDecl::Create(
      ast_ctx, decl_ctx, clang::SourceLocation(), clang::SourceLocation(),
      CreateIdentifier(ast_ctx, name), GetQualType(ast_ctx, type), nullptr,
      clang::SC_None);
}

}  // namespace

IRToASTVisitor::IRToASTVisitor(clang::ASTContext &ctx) : ast_ctx(ctx) {}

clang::FunctionDecl *IRToASTVisitor::GetFunctionDecl(llvm::Instruction *inst) {
  return inst->getParent() ? clang::dyn_cast<clang::FunctionDecl>(
                                 decls[inst->getParent()->getParent()])
                           : nullptr;
}

clang::Expr *IRToASTVisitor::GetOperandExpr(clang::DeclContext *decl_ctx,
                                            llvm::Value *val) {
  DLOG(INFO) << "Getting Expr for " << rellic::LLVMThingToString(val);
  clang::Expr *result = nullptr;

  if (auto cexpr = llvm::dyn_cast<llvm::ConstantExpr>(val)) {
    // Operand is an expression with constant value
    auto inst = cexpr->getAsInstruction();
    visit(inst);
    stmts[val] = stmts[inst];
    stmts.erase(inst);
    DeleteValue(inst);
    result = clang::cast<clang::Expr>(stmts[val]);
  } else if (auto cdata = llvm::dyn_cast<llvm::ConstantData>(val)) {
    // Operand is a literal constant
    result = CreateLiteralExpr(ast_ctx, decl_ctx, cdata);
  } else if (decls.count(val)) {
    // Operand is an l-value (variable, function, ...)
    auto decl = clang::cast<clang::ValueDecl>(decls[val]);
    result = CreateDeclRefExpr(ast_ctx, decl);
    if (llvm::isa<llvm::GlobalValue>(val)) {
      // LLVM IR global values are constant pointers
      auto ref_type = result->getType();
      if (ref_type->isArrayType()) {
        // Add an implicit cast
        result = clang::ImplicitCastExpr::Create(
            ast_ctx, ast_ctx.getPointerType(ref_type),
            clang::CK_ArrayToPointerDecay, result, nullptr, clang::VK_RValue);
      } else {
        // Add a `&` operator
        result = CreateParenExpr(
            ast_ctx, CreateUnaryOperator(ast_ctx, clang::UO_AddrOf, result,
                                         ast_ctx.getPointerType(ref_type)));
      }
    }
  } else if (stmts.count(val)) {
    // Operand is a result of an expression
    result = clang::cast<clang::Expr>(stmts[val]);
  } else {
    LOG(FATAL) << "Invalid operand";
  }

  return result;
}

clang::Stmt *IRToASTVisitor::GetOrCreateStmt(llvm::Value *val) {
  auto &stmt = stmts[val];
  if (stmt) {
    return stmt;
  }

  if (auto inst = llvm::dyn_cast<llvm::Instruction>(val)) {
    visit(inst->getParent());
  } else {
    LOG(FATAL) << "Unsupported value type";
  }

  return stmt;
}

clang::Decl *IRToASTVisitor::GetOrCreateDecl(llvm::Value *val) {
  auto &decl = decls[val];
  if (decl) {
    return decl;
  }

  if (auto func = llvm::dyn_cast<llvm::Function>(val)) {
    VisitFunctionDecl(*func);
  } else if (auto gvar = llvm::dyn_cast<llvm::GlobalVariable>(val)) {
    VisitGlobalVar(*gvar);
  } else if (auto inst = llvm::dyn_cast<llvm::AllocaInst>(val)) {
    visitAllocaInst(*inst);
  } else {
    LOG(FATAL) << "Unsupported value type";
  }

  return decl;
}

void IRToASTVisitor::VisitGlobalVar(llvm::GlobalVariable &gvar) {
  DLOG(INFO) << "VisitGlobalVar: " << rellic::LLVMThingToString(&gvar);
  auto &var = decls[&gvar];
  if (var) {
    return;
  }

  auto name = gvar.getName().str();
  auto tudecl = ast_ctx.getTranslationUnitDecl();
  auto type = llvm::cast<llvm::PointerType>(gvar.getType())->getElementType();

  var = CreateVarDecl(ast_ctx, tudecl, type, name);

  if (gvar.hasInitializer()) {
    auto tmp = clang::cast<clang::VarDecl>(var);
    auto cdata = llvm::cast<llvm::ConstantData>(gvar.getInitializer());
    tmp->setInit(CreateLiteralExpr(ast_ctx, tudecl, cdata));
  }

  tudecl->addDecl(var);
}

void IRToASTVisitor::VisitFunctionDecl(llvm::Function &func) {
  auto name = func.getName().str();
  DLOG(INFO) << "VisitFunctionDecl: " << name;
  auto &decl = decls[&func];
  if (decl) {
    return;
  }

  DLOG(INFO) << "Creating FunctionDecl for " << name;
  auto tudecl = ast_ctx.getTranslationUnitDecl();
  auto type = llvm::cast<llvm::PointerType>(func.getType())->getElementType();

  decl = CreateFunctionDecl(ast_ctx, tudecl, CreateIdentifier(ast_ctx, name),
                            GetQualType(ast_ctx, type));

  tudecl->addDecl(decl);

  if (func.arg_empty()) {
    return;
  }

  auto func_ctx = clang::cast<clang::FunctionDecl>(decl);
  std::vector<clang::ParmVarDecl *> params;
  for (auto &arg : func.args()) {
    auto arg_name = arg.hasName() ? arg.getName().str()
                                  : "arg" + std::to_string(arg.getArgNo());

    DLOG(INFO) << "Creating ParmVarDecl for " << arg_name;

    auto param = CreateParmVarDecl(ast_ctx, func_ctx,
                                   CreateIdentifier(ast_ctx, arg_name),
                                   GetQualType(ast_ctx, arg.getType()));

    decls[&arg] = param;
    params.push_back(param);
  }

  func_ctx->setParams(params);
}

void IRToASTVisitor::visitCallInst(llvm::CallInst &inst) {
  DLOG(INFO) << "visitCallInst: " << rellic::LLVMThingToString(&inst);
  auto &callexpr = stmts[&inst];
  if (callexpr) {
    return;
  }

  auto callee = inst.getCalledValue();
  if (auto func = llvm::dyn_cast<llvm::Function>(callee)) {
    auto decl = decls[func];
    CHECK(decl) << "FunctionDecl for callee does not exist";
    auto fdecl = clang::cast<clang::FunctionDecl>(decl);
    auto fcast = clang::ImplicitCastExpr::Create(
        ast_ctx, ast_ctx.getPointerType(fdecl->getType()),
        clang::CK_FunctionToPointerDecay, CreateDeclRefExpr(ast_ctx, fdecl),
        nullptr, clang::VK_RValue);

    std::vector<clang::Expr *> args;
    for (auto &arg : inst.arg_operands()) {
      auto arg_expr = GetOperandExpr(fdecl, arg);
      CHECK(arg_expr) << "Expr for call operand does not exist";
      auto arg_cast = clang::ImplicitCastExpr::Create(
          ast_ctx, arg_expr->getType(), clang::CK_LValueToRValue, arg_expr,
          nullptr, clang::VK_RValue);
      args.push_back(arg_cast);
    }
    callexpr = CreateCallExpr(ast_ctx, fcast, args, fdecl->getReturnType());
  } else {
    LOG(FATAL) << "Callee is not a function";
  }
}

void IRToASTVisitor::visitGetElementPtrInst(llvm::GetElementPtrInst &inst) {
  DLOG(INFO) << "visitGetElementPtrInst: " << rellic::LLVMThingToString(&inst);
  auto &ref = stmts[&inst];
  if (ref) {
    return;
  }

  auto src_type = inst.getSourceElementType();
  if (llvm::isa<llvm::ArrayType>(src_type)) {
    DLOG(INFO) << "Indexing an array";
    auto ptr = inst.getPointerOperand();
    auto fdecl = GetFunctionDecl(&inst);
    std::vector<llvm::Value *> idxs;
    for (auto &gep_idx : llvm::make_range(inst.idx_begin(), inst.idx_end())) {
      idxs.push_back(gep_idx.get());
      auto gep_type = llvm::GetElementPtrInst::getGEPReturnType(ptr, idxs);
      auto ref_type = GetQualType(ast_ctx, gep_type);
      auto ref_base =
          ref ? clang::cast<clang::Expr>(ref) : GetOperandExpr(fdecl, ptr);
      auto sub = CreateArraySubscriptExpr(ast_ctx, ref_base,
                                          GetOperandExpr(fdecl, gep_idx),
                                          ref_type->getPointeeType());
      ref = CreateParenExpr(
          ast_ctx,
          CreateUnaryOperator(ast_ctx, clang::UO_AddrOf, sub, ref_type));
    }
  } else if (llvm::isa<llvm::StructType>(src_type)) {
    DLOG(INFO) << "Indexing a structure";
  } else {
    LOG(FATAL) << "Indexing an unknown pointer type";
  }
}

void IRToASTVisitor::visitAllocaInst(llvm::AllocaInst &inst) {
  DLOG(INFO) << "visitAllocaInst: " << rellic::LLVMThingToString(&inst);
  auto &declstmt = stmts[&inst];
  if (declstmt) {
    return;
  }

  auto &var = decls[&inst];
  if (!var) {
    auto fdecl = GetFunctionDecl(&inst);

    CHECK(fdecl) << "Undeclared function";

    auto name = inst.getName().str();
    if (name.empty()) {
      auto var_num = std::distance(fdecl->decls_begin(), fdecl->decls_end());
      name = "var" + std::to_string(var_num);
    }

    var = CreateVarDecl(ast_ctx, fdecl, inst.getAllocatedType(), name);
    fdecl->addDecl(var);
  }

  declstmt = new (ast_ctx)
      clang::DeclStmt(clang::DeclGroupRef(var), clang::SourceLocation(),
                      clang::SourceLocation());
}

void IRToASTVisitor::visitStoreInst(llvm::StoreInst &inst) {
  DLOG(INFO) << "visitStoreInst: " << rellic::LLVMThingToString(&inst);
  auto &assign = stmts[&inst];
  if (assign) {
    return;
  }
  // Stores in LLVM IR correspond to value assignments in C
  auto fdecl = GetFunctionDecl(&inst);
  CHECK(fdecl) << "Undeclared function";
  // Get the operand we're assigning to
  auto ptr = inst.getPointerOperand();
  auto lhs = GetOperandExpr(fdecl, ptr);
  CHECK(lhs) << "Invalid assigned-to operand";
  // Get the operand we're assigning from
  auto val = inst.getValueOperand();
  auto rhs = GetOperandExpr(fdecl, val);
  CHECK(rhs) << "Invalid assigned-from operand";
  // Create the assignemnt itself
  assign = CreateBinaryOperator(
      ast_ctx, clang::BO_Assign, lhs, rhs,
      GetQualType(ast_ctx, ptr->getType()->getPointerElementType()));
}

void IRToASTVisitor::visitLoadInst(llvm::LoadInst &inst) {
  DLOG(INFO) << "visitLoadInst: " << rellic::LLVMThingToString(&inst);
  auto &ref = stmts[&inst];
  if (ref) {
    return;
  }

  auto fdecl = GetFunctionDecl(&inst);
  CHECK(fdecl) << "Undeclared function";

  auto ptr = inst.getPointerOperand();
  if (llvm::isa<llvm::AllocaInst>(ptr) ||
      llvm::isa<llvm::GlobalVariable>(ptr)) {
    DLOG(INFO) << "Loading from a variable";
    if (auto var = clang::dyn_cast<clang::VarDecl>(GetOrCreateDecl(ptr))) {
      ref = CreateDeclRefExpr(ast_ctx, var);
    } else {
      LOG(FATAL) << "Referencing undeclared variable";
    }
  } else if (llvm::isa<llvm::GEPOperator>(ptr)) {
    DLOG(INFO) << "Loading from an aggregate";
    auto op = GetOperandExpr(fdecl, ptr);
    auto res_type = GetQualType(ast_ctx, inst.getType());
    ref = CreateUnaryOperator(ast_ctx, clang::UO_Deref, op, res_type);
  } else {
    LOG(FATAL) << "Loading from an unknown pointer";
  }
}

void IRToASTVisitor::visitReturnInst(llvm::ReturnInst &inst) {
  DLOG(INFO) << "visitReturnInst: " << rellic::LLVMThingToString(&inst);
  auto &retstmt = stmts[&inst];
  if (retstmt) {
    return;
  }

  if (auto retval = inst.getReturnValue()) {
    auto fdecl = GetFunctionDecl(&inst);
    auto retexpr = GetOperandExpr(fdecl, retval);
    retstmt = CreateReturnStmt(ast_ctx, retexpr);
  } else {
    retstmt = CreateReturnStmt(ast_ctx, nullptr);
  }
}

void IRToASTVisitor::visitBinaryOperator(llvm::BinaryOperator &inst) {
  DLOG(INFO) << "visitBinaryOperator: " << rellic::LLVMThingToString(&inst);
  auto &binop = stmts[&inst];
  if (binop) {
    return;
  }
  // Get declaration context
  auto fdecl = GetFunctionDecl(&inst);
  // Get operands
  auto lhs = GetOperandExpr(fdecl, inst.getOperand(0));
  auto rhs = GetOperandExpr(fdecl, inst.getOperand(1));
  // Convenience wrapper
  auto BinOpExpr = [this, &lhs, &rhs](clang::BinaryOperatorKind opc,
                                      clang::QualType res_type) {
    return CreateBinaryOperator(ast_ctx, opc, lhs, rhs, res_type);
  };
  // Where the magic happens
  switch (inst.getOpcode()) {
    case llvm::BinaryOperator::URem:
      binop = BinOpExpr(clang::BO_Rem, lhs->getType());
      break;

    case llvm::BinaryOperator::Add: {
      auto type = GetQualType(ast_ctx, inst.getType());
      binop = BinOpExpr(clang::BO_Add, type);
    } break;

    default:
      LOG(FATAL) << "Unknown BinaryOperator operation";
      break;
  }
}

void IRToASTVisitor::visitCmpInst(llvm::CmpInst &inst) {
  DLOG(INFO) << "visitCmpInst: " << rellic::LLVMThingToString(&inst);
  auto &cmp = stmts[&inst];
  if (cmp) {
    return;
  }
  // Get declaration context
  auto fdecl = GetFunctionDecl(&inst);
  // Get operands
  auto lhs = GetOperandExpr(fdecl, inst.getOperand(0));
  auto rhs = GetOperandExpr(fdecl, inst.getOperand(1));
  // Convenience wrapper
  auto CmpExpr = [this, &lhs, &rhs](clang::BinaryOperatorKind opc) {
    return CreateBinaryOperator(ast_ctx, opc, lhs, rhs, ast_ctx.BoolTy);
  };
  // Where the magic happens
  switch (inst.getPredicate()) {
    case llvm::CmpInst::ICMP_EQ:
      cmp = CmpExpr(clang::BO_EQ);
      break;

    case llvm::CmpInst::ICMP_NE:
      cmp = CmpExpr(clang::BO_NE);
      break;

    default:
      LOG(FATAL) << "Unknown CmpInst predicate";
      break;
  }
}

}  // namespace rellic