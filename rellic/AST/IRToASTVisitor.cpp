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

#include <iterator>

namespace rellic {

IRToASTVisitor::IRToASTVisitor(clang::ASTContext &ctx) : ast_ctx(ctx) {}

clang::QualType IRToASTVisitor::GetQualType(llvm::Type *type) {
  DLOG(INFO) << "GetQualType: " << LLVMThingToString(type);
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

clang::Expr *IRToASTVisitor::CreateLiteralExpr(llvm::Constant *constant) {
  DLOG(INFO) << "Creating literal Expr for " << LLVMThingToString(constant);

  clang::Expr *result = nullptr;

  auto CreateInitListLiteral = [this, &constant] {
    std::vector<clang::Expr *> init_exprs;
    for (auto i = 0U; auto elm = constant->getAggregateElement(i); ++i) {
      init_exprs.push_back(GetOperandExpr(elm));
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

    case llvm::Type::PointerTyID: {
      CHECK(llvm::isa<llvm::ConstantPointerNull>(constant))
          << "Non-null pointer constant?";
      result = CreateNullPointerExpr(ast_ctx);
    } break;

    case llvm::Type::ArrayTyID: {
      auto elm_type = llvm::cast<llvm::ArrayType>(l_type)->getElementType();
      if (elm_type->isIntegerTy(8)) {
        std::string init = "";
        if (auto arr = llvm::dyn_cast<llvm::ConstantDataArray>(constant)) {
          init = arr->getAsString();
        }
        result = CreateStringLiteral(ast_ctx, init, c_type);
      } else {
        result = CreateInitListLiteral();
      }
    } break;

    case llvm::Type::StructTyID: {
      result = CreateInitListLiteral();
    } break;

    case llvm::Type::VectorTyID: {
      LOG(FATAL) << "Unimplemented VectorTyID";
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

clang::Expr *IRToASTVisitor::GetOperandExpr(llvm::Value *val) {
  DLOG(INFO) << "Getting Expr for " << LLVMThingToString(val);
  // Operand is a constant value
  if (llvm::isa<llvm::ConstantExpr>(val) ||
      llvm::isa<llvm::ConstantAggregate>(val) ||
      llvm::isa<llvm::ConstantData>(val)) {
    return clang::cast<clang::Expr>(GetOrCreateStmt(val));
  }
  // Helper function for creating declaration references
  auto CreateRef = [this, &val] {
    return CreateDeclRefExpr(
        ast_ctx, clang::cast<clang::ValueDecl>(GetOrCreateDecl(val)));
  };
  // Operand is an l-value (variable, function, ...)
  if (llvm::isa<llvm::GlobalValue>(val) || llvm::isa<llvm::AllocaInst>(val)) {
    clang::Expr *ref = CreateRef();
    // Add an implicit array-to-pointer decay cast in case the l-value
    // is an array, otherwise add a `&` operator.
    auto ref_type = ref->getType();
    auto ptr_type = ast_ctx.getPointerType(ref_type);
    if (ref_type->isArrayType()) {
      ref = CreateImplicitCastExpr(ast_ctx, ptr_type,
                                   clang::CK_ArrayToPointerDecay, ref);
    } else {
      ref = CreateParenExpr(
          ast_ctx,
          CreateUnaryOperator(ast_ctx, clang::UO_AddrOf, ref, ptr_type));
    }
    return ref;
  }
  // Operand is a function argument or local variable
  if (llvm::isa<llvm::Argument>(val)) {
    return CreateRef();
  }
  // Operand is a result of an expression
  if (llvm::isa<llvm::Instruction>(val)) {
    return clang::cast<clang::Expr>(GetOrCreateStmt(val));
  }

  LOG(FATAL) << "Invalid operand";

  return nullptr;
}

clang::Stmt *IRToASTVisitor::GetOrCreateStmt(llvm::Value *val) {
  auto &stmt = stmts[val];
  if (stmt) {
    return stmt;
  }
  // ConstantExpr
  if (auto cexpr = llvm::dyn_cast<llvm::ConstantExpr>(val)) {
    auto inst = cexpr->getAsInstruction();
    stmt = GetOrCreateStmt(inst);
    stmts.erase(inst);
    DeleteValue(inst);
    return stmt;
  }
  // ConstantAggregate
  if (auto caggr = llvm::dyn_cast<llvm::ConstantAggregate>(val)) {
    stmt = CreateLiteralExpr(caggr);
    return stmt;
  }
  // ConstantData
  if (auto cdata = llvm::dyn_cast<llvm::ConstantData>(val)) {
    stmt = CreateLiteralExpr(cdata);
    return stmt;
  }
  // Instruction
  if (auto inst = llvm::dyn_cast<llvm::Instruction>(val)) {
    visit(inst);
    return stmt;
  }

  LOG(FATAL) << "Unsupported value type";

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
  } else if (auto arg = llvm::dyn_cast<llvm::Argument>(val)) {
    VisitArgument(*arg);
  } else if (auto inst = llvm::dyn_cast<llvm::AllocaInst>(val)) {
    visitAllocaInst(*inst);
  } else {
    LOG(FATAL) << "Unsupported value type";
  }

  return decl;
}

void IRToASTVisitor::VisitGlobalVar(llvm::GlobalVariable &gvar) {
  DLOG(INFO) << "VisitGlobalVar: " << LLVMThingToString(&gvar);
  auto &var = value_decls[&gvar];
  if (var) {
    return;
  }

  auto name = gvar.getName().str();
  auto tudecl = ast_ctx.getTranslationUnitDecl();
  auto type = llvm::cast<llvm::PointerType>(gvar.getType())->getElementType();

  var = CreateVarDecl(tudecl, type, name);
  // Create an initalizer literal
  if (gvar.hasInitializer()) {
    clang::cast<clang::VarDecl>(var)->setInit(
        GetOperandExpr(gvar.getInitializer()));
  }
  // Add the global var
  tudecl->addDecl(var);
}

void IRToASTVisitor::VisitArgument(llvm::Argument &arg) {
  DLOG(INFO) << "VisitArgument: " << LLVMThingToString(&arg);
  auto &parm = value_decls[&arg];
  if (parm) {
    return;
  }
  // Create a name
  auto name = arg.hasName() ? arg.getName().str()
                            : "arg" + std::to_string(arg.getArgNo());
  // Get parent function declaration
  auto func = arg.getParent();
  auto fdecl = clang::cast<clang::FunctionDecl>(GetOrCreateDecl(func));
  // Create a declaration
  parm = CreateParmVarDecl(ast_ctx, fdecl, CreateIdentifier(ast_ctx, name),
                           GetQualType(arg.getType()));
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

  std::vector<clang::ParmVarDecl *> params;
  for (auto &arg : func.args()) {
    auto parm = clang::cast<clang::ParmVarDecl>(GetOrCreateDecl(&arg));
    params.push_back(parm);
  }

  decl->getAsFunction()->setParams(params);
}

void IRToASTVisitor::visitCallInst(llvm::CallInst &inst) {
  DLOG(INFO) << "visitCallInst: " << LLVMThingToString(&inst);
  auto &callexpr = stmts[&inst];
  if (callexpr) {
    return;
  }

  auto callee = inst.getCalledValue();
  if (auto func = llvm::dyn_cast<llvm::Function>(callee)) {
    auto fdecl = GetOrCreateDecl(func)->getAsFunction();
    auto fcast = CreateImplicitCastExpr(
        ast_ctx, ast_ctx.getPointerType(fdecl->getType()),
        clang::CK_FunctionToPointerDecay, CreateDeclRefExpr(ast_ctx, fdecl));

    std::vector<clang::Expr *> args;
    for (auto &arg : inst.arg_operands()) {
      auto arg_expr = GetOperandExpr(arg);
      auto arg_cast = CreateImplicitCastExpr(
          ast_ctx, arg_expr->getType(), clang::CK_LValueToRValue, arg_expr);
      args.push_back(arg_cast);
    }
    callexpr = CreateCallExpr(ast_ctx, fcast, args, fdecl->getReturnType());
  } else {
    LOG(FATAL) << "Callee is not a function";
  }
}

void IRToASTVisitor::visitGetElementPtrInst(llvm::GetElementPtrInst &inst) {
  DLOG(INFO) << "visitGetElementPtrInst: " << LLVMThingToString(&inst);
  auto &ref = stmts[&inst];
  if (ref) {
    return;
  }

  auto indexed_type = inst.getPointerOperandType();
  auto base = GetOperandExpr(inst.getPointerOperand());

  auto IndexArrayOrPtr = [&](llvm::Value &gep_idx) {
    auto idx = GetOperandExpr(&gep_idx);
    auto type = GetQualType(indexed_type);
    base = CreateArraySubscriptExpr(ast_ctx, base, idx, type);
  };

  auto IndexStruct = [&](llvm::Value &gep_idx) {
    auto mem_idx = llvm::dyn_cast<llvm::ConstantInt>(&gep_idx);
    CHECK(mem_idx) << "Non-constant GEP index while indexing a structure";
    auto type = indexed_type;
    auto tdecl = type_decls[type];
    CHECK(tdecl) << "Structure declaration doesn't exist";
    auto record = clang::cast<clang::RecordDecl>(tdecl);
    auto field_it = record->field_begin();
    std::advance(field_it, mem_idx->getLimitedValue());
    CHECK(field_it != record->field_end()) << "GEP index is out of bounds";
    base = CreateMemberExpr(ast_ctx, base, *field_it, GetQualType(type),
                            /*is_arrow=*/true);
  };

  for (auto &idx : inst.indices()) {
    switch (indexed_type->getTypeID()) {
      // Initial pointer
      case llvm::Type::PointerTyID: {
        CHECK(idx == *inst.idx_begin())
            << "Indexing an llvm::PointerType is only valid at first index";
        IndexArrayOrPtr(*idx);
        auto type = llvm::cast<llvm::PointerType>(indexed_type);
        indexed_type = type->getElementType();
      } break;
      // Arrays
      case llvm::Type::ArrayTyID: {
        IndexArrayOrPtr(*idx);
        auto type = llvm::cast<llvm::ArrayType>(indexed_type);
        indexed_type = type->getTypeAtIndex(idx);
      } break;
      // Structures
      case llvm::Type::StructTyID: {
        IndexStruct(*idx);
        auto type = llvm::cast<llvm::StructType>(indexed_type);
        indexed_type = type->getTypeAtIndex(idx);
      } break;
      // Unknown
      default:
        LOG(FATAL) << "Indexing an unknown pointer type";
        break;
    }
    // Take address via `(&base)`
    base = CreateParenExpr(
        ast_ctx, CreateUnaryOperator(ast_ctx, clang::UO_AddrOf, base,
                                     ast_ctx.getPointerType(base->getType())));
  }

  ref = base;
}

void IRToASTVisitor::visitAllocaInst(llvm::AllocaInst &inst) {
  DLOG(INFO) << "visitAllocaInst: " << LLVMThingToString(&inst);
  auto &declstmt = stmts[&inst];
  if (declstmt) {
    return;
  }

  auto func = inst.getFunction();
  CHECK(func) << "AllocaInst does not have a parent function";

  auto &var = value_decls[&inst];
  if (!var) {
    auto fdecl = clang::cast<clang::FunctionDecl>(GetOrCreateDecl(func));
    auto name = inst.getName().str();
    if (name.empty()) {
      auto var_num = std::distance(fdecl->decls_begin(), fdecl->decls_end());
      name = "var" + std::to_string(var_num);
    }

    var = CreateVarDecl(fdecl, inst.getAllocatedType(), name);
    fdecl->addDecl(var);
  }

  declstmt = CreateDeclStmt(ast_ctx, var);
}

void IRToASTVisitor::visitStoreInst(llvm::StoreInst &inst) {
  DLOG(INFO) << "visitStoreInst: " << LLVMThingToString(&inst);
  auto &assign = stmts[&inst];
  if (assign) {
    return;
  }
  // Stores in LLVM IR correspond to value assignments in C
  // Get the operand we're assigning to
  auto ptr = inst.getPointerOperand();
  auto lhs = GetOperandExpr(ptr);
  // Get the operand we're assigning from
  auto val = inst.getValueOperand();
  auto rhs = GetOperandExpr(val);
  // Create the assignemnt itself
  auto type = GetQualType(ptr->getType()->getPointerElementType());
  assign = CreateBinaryOperator(
      ast_ctx, clang::BO_Assign,
      CreateUnaryOperator(ast_ctx, clang::UO_Deref, lhs, type), rhs, type);
}

void IRToASTVisitor::visitLoadInst(llvm::LoadInst &inst) {
  DLOG(INFO) << "visitLoadInst: " << LLVMThingToString(&inst);
  auto &ref = stmts[&inst];
  if (ref) {
    return;
  }

  auto ptr = inst.getPointerOperand();
  if (llvm::isa<llvm::AllocaInst>(ptr) ||
      llvm::isa<llvm::GlobalVariable>(ptr) ||
      llvm::isa<llvm::GEPOperator>(ptr)) {
    auto op = GetOperandExpr(ptr);
    auto res_type = GetQualType(inst.getType());
    ref = CreateUnaryOperator(ast_ctx, clang::UO_Deref, op, res_type);
  } else {
    LOG(FATAL) << "Loading from an unknown pointer";
  }
}

void IRToASTVisitor::visitReturnInst(llvm::ReturnInst &inst) {
  DLOG(INFO) << "visitReturnInst: " << LLVMThingToString(&inst);
  auto &retstmt = stmts[&inst];
  if (retstmt) {
    return;
  }

  if (auto retval = inst.getReturnValue()) {
    auto retexpr = GetOperandExpr(retval);
    retstmt = CreateReturnStmt(ast_ctx, retexpr);
  } else {
    retstmt = CreateReturnStmt(ast_ctx, nullptr);
  }
}

void IRToASTVisitor::visitBinaryOperator(llvm::BinaryOperator &inst) {
  DLOG(INFO) << "visitBinaryOperator: " << LLVMThingToString(&inst);
  auto &binop = stmts[&inst];
  if (binop) {
    return;
  }
  // Get operands
  auto lhs = GetOperandExpr(inst.getOperand(0));
  auto rhs = GetOperandExpr(inst.getOperand(1));
  // Convenience wrapper
  auto BinOpExpr = [this, &lhs, &rhs](clang::BinaryOperatorKind opc,
                                      clang::QualType res_type) {
    return CreateBinaryOperator(ast_ctx, opc, lhs, rhs, res_type);
  };
  // Get result type
  auto type = GetQualType(inst.getType());
  // Where the magic happens
  switch (inst.getOpcode()) {
    case llvm::BinaryOperator::LShr: {
      auto size = ast_ctx.getTypeSize(lhs->getType());
      auto sign = ast_ctx.getIntTypeForBitwidth(size, 1);
      lhs = CreateCStyleCastExpr(ast_ctx, sign,
                                 clang::CastKind::CK_IntegralCast, lhs);
      binop = BinOpExpr(clang::BO_Shr, type);
    } break;

    case llvm::BinaryOperator::Xor:
      binop = BinOpExpr(clang::BO_Xor, type);
      break;

    case llvm::BinaryOperator::URem:
      binop = BinOpExpr(clang::BO_Rem, lhs->getType());
      break;

    case llvm::BinaryOperator::Add:
      binop = BinOpExpr(clang::BO_Add, type);
      break;

    default:
      LOG(FATAL) << "Unknown BinaryOperator operation";
      break;
  }
}

void IRToASTVisitor::visitCmpInst(llvm::CmpInst &inst) {
  DLOG(INFO) << "visitCmpInst: " << LLVMThingToString(&inst);
  auto &cmp = stmts[&inst];
  if (cmp) {
    return;
  }
  // Get operands
  auto lhs = GetOperandExpr(inst.getOperand(0));
  auto rhs = GetOperandExpr(inst.getOperand(1));
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

void IRToASTVisitor::visitPtrToInt(llvm::PtrToIntInst &inst) {
  DLOG(INFO) << "visitPtrToInt: " << LLVMThingToString(&inst);
  auto &cast = stmts[&inst];
  if (cast) {
    return;
  }
  // Get ptrtoint operand
  auto operand = GetOperandExpr(inst.getOperand(0));
  // Create the cast
  cast = CreateCStyleCastExpr(ast_ctx, GetQualType(inst.getType()),
                              clang::CastKind::CK_PointerToIntegral, operand);
}

void IRToASTVisitor::visitTruncInst(llvm::TruncInst &inst) {
  LOG(FATAL) << "Unimplemented llvm::TruncInst visitor";
}

}  // namespace rellic