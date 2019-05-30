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

// #define GOOGLE_STRIP_LOG 1

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/BC/Compat/Value.h"
#include "rellic/BC/Util.h"

#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/Util.h"

namespace rellic {

IRToASTVisitor::IRToASTVisitor(clang::ASTContext &ctx) : ast_ctx(ctx) {}

clang::QualType IRToASTVisitor::GetQualType(llvm::Type *type) {
  DLOG(INFO) << "GetQualType: " << rellic::LLVMThingToString(type);
  clang::QualType result;
  switch (type->getTypeID()) {
    case llvm::Type::VoidTyID:
      result = ast_ctx.VoidTy;
      break;

    case llvm::Type::HalfTyID:
      result = ast_ctx.HalfTy;
      break;

    case llvm::Type::FloatTyID:
      result = ast_ctx.FloatTy;
      break;

    case llvm::Type::DoubleTyID:
      result = ast_ctx.DoubleTy;
      break;

    case llvm::Type::IntegerTyID: {
      auto size = type->getIntegerBitWidth();
      CHECK(size > 0) << "Integer bit width has to be greater than 0";
      result = ast_ctx.getIntTypeForBitwidth(size, 0);
    } break;

    case llvm::Type::FunctionTyID: {
      auto func = llvm::cast<llvm::FunctionType>(type);
      auto ret = GetQualType(func->getReturnType());
      std::vector<clang::QualType> params;
      for (auto param : func->params()) {
        params.push_back(GetQualType(param));
      }
      auto epi = clang::FunctionProtoType::ExtProtoInfo();
      epi.Variadic = func->isVarArg();
      result = ast_ctx.getFunctionType(ret, params, epi);
    } break;

    case llvm::Type::PointerTyID: {
      auto ptr = llvm::cast<llvm::PointerType>(type);
      result = ast_ctx.getPointerType(GetQualType(ptr->getElementType()));
    } break;

    case llvm::Type::ArrayTyID: {
      auto arr = llvm::cast<llvm::ArrayType>(type);
      auto elm = GetQualType(arr->getElementType());
      result = ast_ctx.getConstantArrayType(
          elm, llvm::APInt(32, arr->getNumElements()),
          clang::ArrayType::ArraySizeModifier::Normal, 0);
    } break;

    case llvm::Type::StructTyID: {
      clang::RecordDecl *sdecl = nullptr;
      auto &decl = type_decls[type];
      if (!decl) {
        static auto scnt = 0U;
        auto tudecl = ast_ctx.getTranslationUnitDecl();
        auto strct = llvm::cast<llvm::StructType>(type);
        auto sname = strct->hasName() ? strct->getName().str()
                                      : "struct_" + std::to_string(scnt++);
        // Create a C struct declaration
        auto sid = CreateIdentifier(ast_ctx, sname);
        decl = sdecl = CreateStructDecl(ast_ctx, tudecl, sid);
        // Add fields to the C struct
        for (auto ecnt = 0U; ecnt < strct->getNumElements(); ++ecnt) {
          auto etype = GetQualType(strct->getElementType(ecnt));
          auto eid = CreateIdentifier(ast_ctx, "field_" + std::to_string(ecnt));
          sdecl->addDecl(CreateFieldDecl(ast_ctx, sdecl, eid, etype));
        }
        // Complete the C struct definition
        sdecl->completeDefinition();
        // Add C struct to translation unit
        tudecl->addDecl(sdecl);
      } else {
        sdecl = clang::cast<clang::RecordDecl>(decl);
      }     
      result = ast_ctx.getRecordType(sdecl);
    } break;

    default:
      LOG(FATAL) << "Unknown LLVM Type";
      break;
  }

  return result;
}

clang::Expr *IRToASTVisitor::CreateLiteralExpr(clang::DeclContext *decl_ctx,
                                               llvm::Constant *constant) {
  DLOG(INFO) << "Creating literal Expr for "
             << rellic::LLVMThingToString(constant);

  clang::Expr *result = nullptr;

  auto CreateInitListLiteral = [this, &decl_ctx, &constant] {
    std::vector<clang::Expr *> init_exprs;
    for (auto i = 0U; i < constant->getNumOperands(); ++i) {
      auto elm = constant->getAggregateElement(i);
      init_exprs.push_back(CreateLiteralExpr(decl_ctx, elm));
    }
    return CreateInitListExpr(ast_ctx, init_exprs);
  };

  auto l_type = constant->getType();
  auto c_type = GetQualType(l_type);

  switch (l_type->getTypeID()) {
    // Floats
    case llvm::Type::HalfTyID:
    case llvm::Type::FloatTyID:
    case llvm::Type::DoubleTyID: {
      auto val = llvm::cast<llvm::ConstantFP>(constant)->getValueAPF();
      result = CreateFloatingLiteral(ast_ctx, val, c_type);
    } break;
    // Integers
    case llvm::Type::IntegerTyID: {
      auto val = llvm::cast<llvm::ConstantInt>(constant)->getValue();
      result = CreateIntegerLiteral(ast_ctx, val, c_type);
    } break;

    case llvm::Type::ArrayTyID: {
      auto arr = llvm::cast<llvm::ConstantDataArray>(constant);
      result = arr->isString()
                   ? CreateStringLiteral(ast_ctx, arr->getAsString(), c_type)
                   : CreateInitListLiteral();
    } break;

    case llvm::Type::StructTyID: {
      result = CreateInitListLiteral();
    } break;

    default:
      LOG(FATAL) << "Unknown LLVM constant type";
      break;
  }

  return result;
}

clang::VarDecl *IRToASTVisitor::CreateVarDecl(clang::DeclContext *decl_ctx,
                                              llvm::Type *type,
                                              std::string name) {
  DLOG(INFO) << "Creating VarDecl for " << name;
  return clang::VarDecl::Create(ast_ctx, decl_ctx, clang::SourceLocation(),
                                clang::SourceLocation(),
                                CreateIdentifier(ast_ctx, name),
                                GetQualType(type), nullptr, clang::SC_None);
}

clang::FunctionDecl *IRToASTVisitor::GetFunctionDecl(llvm::Instruction *inst) {
  return inst->getParent() ? clang::dyn_cast<clang::FunctionDecl>(
                                 value_decls[inst->getParent()->getParent()])
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
    result = CreateLiteralExpr(decl_ctx, cdata);
  } else if (value_decls.count(val)) {
    // Operand is an l-value (variable, function, ...)
    result = CreateDeclRefExpr(ast_ctx, value_decls[val]);
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
  auto &decl = value_decls[val];
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
  auto &var = value_decls[&gvar];
  if (var) {
    return;
  }

  auto name = gvar.getName().str();
  auto tudecl = ast_ctx.getTranslationUnitDecl();
  auto type = llvm::cast<llvm::PointerType>(gvar.getType())->getElementType();

  var = CreateVarDecl(tudecl, type, name);

  if (gvar.hasInitializer()) {
    clang::cast<clang::VarDecl>(var)->setInit(
        CreateLiteralExpr(tudecl, gvar.getInitializer()));
  }

  tudecl->addDecl(var);
}

void IRToASTVisitor::VisitFunctionDecl(llvm::Function &func) {
  auto name = func.getName().str();
  DLOG(INFO) << "VisitFunctionDecl: " << name;
  auto &decl = value_decls[&func];
  if (decl) {
    return;
  }

  DLOG(INFO) << "Creating FunctionDecl for " << name;
  auto tudecl = ast_ctx.getTranslationUnitDecl();
  auto type = llvm::cast<llvm::PointerType>(func.getType())->getElementType();

  decl = CreateFunctionDecl(ast_ctx, tudecl, CreateIdentifier(ast_ctx, name),
                            GetQualType(type));

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
                                   GetQualType(arg.getType()));

    value_decls[&arg] = param;
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
    auto decl = value_decls[func];
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
      auto ref_type = GetQualType(gep_type);
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
    LOG(FATAL) << "Indexing a structure";
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

  auto &var = value_decls[&inst];
  if (!var) {
    auto fdecl = GetFunctionDecl(&inst);

    CHECK(fdecl) << "Undeclared function";

    auto name = inst.getName().str();
    if (name.empty()) {
      auto var_num = std::distance(fdecl->decls_begin(), fdecl->decls_end());
      name = "var" + std::to_string(var_num);
    }

    var = CreateVarDecl(fdecl, inst.getAllocatedType(), name);
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
      GetQualType(ptr->getType()->getPointerElementType()));
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
    auto res_type = GetQualType(inst.getType());
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
      auto type = GetQualType(inst.getType());
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