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

#include "rellic/AST/IRToASTVisitor.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <iterator>

#include "rellic/AST/Util.h"
#include "rellic/BC/Compat/Value.h"
#include "rellic/BC/Util.h"

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

    case llvm::Type::X86_FP80TyID:
      result = ast_ctx.LongDoubleTy;
      break;

    case llvm::Type::IntegerTyID: {
      auto size = type->getIntegerBitWidth();
      CHECK(size > 0) << "Integer bit width has to be greater than 0";
      result = ast_ctx.getIntTypeForBitwidth(size, 0);
          // size == 1 ? ast_ctx.BoolTy : ast_ctx.getIntTypeForBitwidth(size, 0);
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
      result = GetConstantArrayType(ast_ctx, elm, arr->getNumElements());
    } break;

    case llvm::Type::StructTyID: {
      clang::RecordDecl *sdecl = nullptr;
      auto &decl = type_decls[type];
      if (!decl) {
        auto tudecl = ast_ctx.getTranslationUnitDecl();
        auto strct = llvm::cast<llvm::StructType>(type);
        auto sname = strct->getName().str();
        if (sname.empty()) {
          auto num = GetNumDecls<clang::TypeDecl>(tudecl);
          sname = "struct" + std::to_string(num);
        }
        // Create a C struct declaration
        auto sid = CreateIdentifier(ast_ctx, sname);
        decl = sdecl = CreateStructDecl(ast_ctx, tudecl, sid);
        // Add fields to the C struct
        for (auto ecnt = 0U; ecnt < strct->getNumElements(); ++ecnt) {
          auto etype = GetQualType(strct->getElementType(ecnt));
          auto eid = CreateIdentifier(ast_ctx, "field" + std::to_string(ecnt));
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

    case llvm::Type::MetadataTyID:
      result = ast_ctx.VoidPtrTy;
      break;

    default:
      LOG(FATAL) << "Unknown LLVM Type";
      break;
  }

  CHECK(!result.isNull()) << "Unknown LLVM Type";

  return result;
}

clang::Expr *IRToASTVisitor::CreateLiteralExpr(llvm::Constant *constant) {
  DLOG(INFO) << "Creating literal Expr for " << LLVMThingToString(constant);

  clang::Expr *result = nullptr;

  auto l_type = constant->getType();
  auto c_type = GetQualType(l_type);

  auto CreateInitListLiteral = [this, &constant, &c_type] {
    std::vector<clang::Expr *> init_exprs;
    if (!constant->isZeroValue()) {
      for (auto i = 0U; auto elm = constant->getAggregateElement(i); ++i) {
        init_exprs.push_back(GetOperandExpr(elm));
      }
    }
    return CreateInitListExpr(ast_ctx, init_exprs, c_type);
  };

  switch (l_type->getTypeID()) {
    // Floats
    case llvm::Type::HalfTyID:
    case llvm::Type::FloatTyID:
    case llvm::Type::DoubleTyID:
    case llvm::Type::X86_FP80TyID: {
      auto val = llvm::cast<llvm::ConstantFP>(constant)->getValueAPF();
      result = CreateFloatingLiteral(ast_ctx, val, c_type);
    } break;
    // Integers
    case llvm::Type::IntegerTyID: {
      auto val = llvm::cast<llvm::ConstantInt>(constant)->getValue();
      switch (clang::cast<clang::BuiltinType>(c_type)->getKind()) {
        case clang::BuiltinType::Kind::UChar:
        case clang::BuiltinType::Kind::SChar:
          result = CreateCharacterLiteral(ast_ctx, val, c_type);
          break;

        case clang::BuiltinType::Kind::Bool:
          result = CreateIntegerLiteral(ast_ctx, val, ast_ctx.IntTy);
          break;

        case clang::BuiltinType::Kind::Short:
        case clang::BuiltinType::Kind::UShort:
        case clang::BuiltinType::Kind::Int:
        case clang::BuiltinType::Kind::UInt:
        case clang::BuiltinType::Kind::Long:
        case clang::BuiltinType::Kind::ULong:
        case clang::BuiltinType::Kind::LongLong:
        case clang::BuiltinType::Kind::ULongLong: {
          result = CreateIntegerLiteral(ast_ctx, val.abs(), c_type);
          if (val.isNegative()) {
            result =
                CreateUnaryOperator(ast_ctx, clang::UO_Minus, result, c_type);
          }
        } break;

        default:
          LOG(FATAL) << "Unsupported integer literal type";
          break;
      }
    } break;

    case llvm::Type::PointerTyID: {
      if (llvm::isa<llvm::ConstantPointerNull>(constant)) {
        result = CreateNullPointerExpr(ast_ctx);
      } else if (llvm::isa<llvm::UndefValue>(constant)) {
        result = CreateUndefExpr(ast_ctx, c_type);
      } else {
        LOG(FATAL) << "Unsupported pointer constant";
      }
    } break;

    case llvm::Type::ArrayTyID: {
      auto elm_type = llvm::cast<llvm::ArrayType>(l_type)->getElementType();
      if (elm_type->isIntegerTy(8)) {
        std::string init = "";
        if (auto arr = llvm::dyn_cast<llvm::ConstantDataArray>(constant)) {
          init = arr->getAsString().str();
        }
        result = CreateStringLiteral(ast_ctx, init, c_type);
      } else {
        result = CreateInitListLiteral();
      }
    } break;

    case llvm::Type::StructTyID: {
      result = CreateInitListLiteral();
    } break;

    default:
      if (l_type->isVectorTy()) {
        LOG(FATAL) << "Unimplemented VectorTyID";
      } else {
        LOG(FATAL) << "Unknown LLVM constant type";
      }
      break;
  }

  return result;
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
    // Add a `&` operator
    ref = CreateParenExpr(
        ast_ctx, CreateUnaryOperator(ast_ctx, clang::UO_AddrOf, ref,
                                     ast_ctx.getPointerType(ref->getType())));
    return ref;
  }
  // Operand is a function argument or local variable
  if (llvm::isa<llvm::Argument>(val)) {
    return CreateRef();
  }
  // Operand is a result of an expression
  if (llvm::isa<llvm::Instruction>(val)) {
    return CreateParenExpr(ast_ctx,
                           clang::cast<clang::Expr>(GetOrCreateStmt(val)));
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

void IRToASTVisitor::SetStmt(llvm::Value *val, clang::Stmt *stmt) {
  stmts[val] = stmt;
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

  auto type = llvm::cast<llvm::PointerType>(gvar.getType())->getElementType();
  auto tudecl = ast_ctx.getTranslationUnitDecl();
  auto name = gvar.getName().str();
  if (name.empty()) {
    auto num = GetNumDecls<clang::VarDecl>(tudecl);
    name = "gvar" + std::to_string(num);
  }
  // Create a variable declaration
  var = CreateVarDecl(ast_ctx, tudecl, CreateIdentifier(ast_ctx, name),
                      GetQualType(type));
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

  auto type = GetQualType(inst.getType());

  std::vector<clang::Expr *> args;
  for (auto &arg : inst.arg_operands()) {
    auto expr = GetOperandExpr(arg);
    auto type = expr->getType();
    auto cast =
        CreateImplicitCastExpr(ast_ctx, type, clang::CK_LValueToRValue, expr);
    args.push_back(cast);
  }

  auto callee = inst.getCalledOperand();
  if (auto func = llvm::dyn_cast<llvm::Function>(callee)) {
    auto decl = GetOrCreateDecl(func)->getAsFunction();
    auto ptr = ast_ctx.getPointerType(decl->getType());
    auto ref = CreateDeclRefExpr(ast_ctx, decl);
    auto cast = CreateImplicitCastExpr(ast_ctx, ptr,
                                       clang::CK_FunctionToPointerDecay, ref);
    callexpr = CreateCallExpr(ast_ctx, cast, args, type);
  } else if (llvm::isa<llvm::PointerType>(callee->getType())) {
    auto ptr = GetOperandExpr(callee);
    callexpr = CreateCallExpr(ast_ctx, ptr, args, type);
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

  auto IndexPtr = [&](llvm::Value &gep_idx) {
    auto base_type = base->getType();
    CHECK(base_type->isPointerType()) << "Operand is not a clang::PointerType";
    auto type = clang::cast<clang::PointerType>(base_type)->getPointeeType();
    auto idx = GetOperandExpr(&gep_idx);
    base = CreateArraySubscriptExpr(ast_ctx, base, idx, type);
  };

  auto IndexStruct = [&](llvm::Value &gep_idx) {
    auto mem_idx = llvm::dyn_cast<llvm::ConstantInt>(&gep_idx);
    CHECK(mem_idx) << "Non-constant GEP index while indexing a structure";
    auto tdecl = type_decls[indexed_type];
    CHECK(tdecl) << "Structure declaration doesn't exist";
    auto record = clang::cast<clang::RecordDecl>(tdecl);
    auto field_it = record->field_begin();
    std::advance(field_it, mem_idx->getLimitedValue());
    CHECK(field_it != record->field_end()) << "GEP index is out of bounds";
    base = CreateMemberExpr(ast_ctx, base, *field_it, field_it->getType(),
                            /*is_arrow=*/false);
  };

  for (auto &idx : llvm::make_range(inst.idx_begin(), inst.idx_end())) {
    switch (indexed_type->getTypeID()) {
      // Initial pointer
      case llvm::Type::PointerTyID: {
        CHECK(idx == *inst.idx_begin())
            << "Indexing an llvm::PointerType is only valid at first index";
        IndexPtr(*idx);
        indexed_type =
            llvm::cast<llvm::PointerType>(indexed_type)->getElementType();
      } break;
      // Arrays
      case llvm::Type::ArrayTyID: {
        base = CreateImplicitCastExpr(
            ast_ctx, ast_ctx.getArrayDecayedType(base->getType()),
            clang::CK_ArrayToPointerDecay, base);
        IndexPtr(*idx);
        indexed_type =
            llvm::cast<llvm::ArrayType>(indexed_type)->getElementType();
      } break;
      // Structures
      case llvm::Type::StructTyID: {
        IndexStruct(*idx);
        indexed_type =
            llvm::cast<llvm::StructType>(indexed_type)->getTypeAtIndex(idx);
      } break;
      // Unknown
      default:
        LOG(FATAL) << "Indexing an unknown pointer type";
        break;
    }
    // Add parens to preserve expression semantics
    base = CreateParenExpr(ast_ctx, base);
  }

  ref = CreateUnaryOperator(ast_ctx, clang::UO_AddrOf, base,
                            ast_ctx.getPointerType(base->getType()));
}

void IRToASTVisitor::visitExtractValueInst(llvm::ExtractValueInst &inst) {
  DLOG(INFO) << "visitExtractValueInst: " << LLVMThingToString(&inst);
  auto &ref = stmts[&inst];
  if (ref) {
    return;
  }

  auto base = GetOperandExpr(inst.getAggregateOperand());
  auto indexed_type = inst.getAggregateOperand()->getType();

  auto IndexPtr = [&](unsigned ev_idx) {
    auto base_type = base->getType();
    CHECK(base_type->isPointerType()) << "Operand is not a clang::PointerType";
    auto type = clang::cast<clang::PointerType>(base_type)->getPointeeType();
    auto idx = CreateIntegerLiteral(
        ast_ctx,
        llvm::APInt(ast_ctx.getIntWidth(ast_ctx.UnsignedIntTy), ev_idx),
        ast_ctx.UnsignedIntTy);
    base = CreateArraySubscriptExpr(ast_ctx, base, idx, type);
  };

  auto IndexStruct = [&](unsigned ev_idx) {
    auto tdecl = type_decls[indexed_type];
    CHECK(tdecl) << "Structure declaration doesn't exist";
    auto record = clang::cast<clang::RecordDecl>(tdecl);
    auto field_it = record->field_begin();
    std::advance(field_it, ev_idx);
    CHECK(field_it != record->field_end())
        << "ExtractValue index is out of bounds";
    base = CreateMemberExpr(ast_ctx, base, *field_it, field_it->getType(),
                            /*is_arrow=*/false);
  };

  for (auto idx : llvm::make_range(inst.idx_begin(), inst.idx_end())) {
    switch (indexed_type->getTypeID()) {
      // Arrays
      case llvm::Type::ArrayTyID: {
        base = CreateImplicitCastExpr(
            ast_ctx, ast_ctx.getArrayDecayedType(base->getType()),
            clang::CK_ArrayToPointerDecay, base);
        IndexPtr(idx);
        indexed_type =
            llvm::cast<llvm::ArrayType>(indexed_type)->getElementType();
      } break;
      // Structures
      case llvm::Type::StructTyID: {
        IndexStruct(idx);
        indexed_type =
            llvm::cast<llvm::StructType>(indexed_type)->getTypeAtIndex(idx);
      } break;

      default:
        LOG(FATAL) << "Indexing an unknown aggregate type";
        break;
    }
    // Add parens to preserve expression semantics
    base = CreateParenExpr(ast_ctx, base);
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
      auto num = GetNumDecls<clang::VarDecl>(fdecl);
      name = "var" + std::to_string(num);
    }

    var = CreateVarDecl(ast_ctx, fdecl, CreateIdentifier(ast_ctx, name),
                        GetQualType(inst.getAllocatedType()));
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
  auto op = GetOperandExpr(ptr);
  auto res_type = GetQualType(inst.getType());

  ref = CreateUnaryOperator(ast_ctx, clang::UO_Deref, op, res_type);
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

clang::Expr *IRToASTVisitor::CastOperand(clang::QualType dst, clang::Expr *op) {
  // Get operand type
  auto src = op->getType();
  // Helpers
  auto IsInt = [this](clang::QualType t) { return t->isIntegralType(ast_ctx); };
  auto IsFloat = [](clang::QualType t) { return t->isFloatingType(); };
  auto IsSmaller = ast_ctx.getTypeSize(src) < ast_ctx.getTypeSize(dst);
  auto MakeCast = [this, dst, op](clang::CastKind kind) {
    return CreateCStyleCastExpr(ast_ctx, dst, kind, op);
  };

  // CK_FloatingCast
  if (IsFloat(dst) && IsFloat(src) && IsSmaller) {
    return MakeCast(clang::CastKind::CK_FloatingCast);
  }
  // CK_IntegralCast
  if (IsInt(dst) && IsInt(src) && IsSmaller) {
    return MakeCast(clang::CastKind::CK_IntegralCast);
  }
  // CK_IntegralToFloatingCast
  if (IsFloat(dst) && IsInt(src)) {
    return MakeCast(clang::CastKind::CK_IntegralToFloating);
  }
  // CK_FloatingToIntegralCast
  if (IsInt(dst) && IsFloat(src)) {
    return MakeCast(clang::CastKind::CK_FloatingToIntegral);
  }
  // Nothing
  return op;
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
  auto BinOpExpr = [this, lhs, rhs](clang::BinaryOperatorKind opc,
                                    clang::QualType type) {
    return CreateBinaryOperator(ast_ctx, opc, CastOperand(rhs->getType(), lhs),
                                CastOperand(lhs->getType(), rhs), type);
  };
  // Sign-cast int operand
  auto IntSignCast = [this](clang::Expr *operand, bool sign) {
    auto type = ast_ctx.getIntTypeForBitwidth(
        ast_ctx.getTypeSize(operand->getType()), sign);
    return CreateCStyleCastExpr(ast_ctx, type, clang::CastKind::CK_IntegralCast,
                                operand);
  };

  // Get result type
  auto type = GetQualType(inst.getType());
  // Where the magic happens
  switch (inst.getOpcode()) {
    case llvm::BinaryOperator::LShr:
      lhs = IntSignCast(lhs, false);
      binop = BinOpExpr(clang::BO_Shr, type);
      break;

    case llvm::BinaryOperator::AShr:
      lhs = IntSignCast(lhs, true);
      binop = BinOpExpr(clang::BO_Shr, type);
      break;

    case llvm::BinaryOperator::Shl:
      binop = BinOpExpr(clang::BO_Shl, type);
      break;

    case llvm::BinaryOperator::And:
      binop = BinOpExpr(clang::BO_And, type);
      break;

    case llvm::BinaryOperator::Or:
      binop = BinOpExpr(clang::BO_Or, type);
      break;

    case llvm::BinaryOperator::Xor:
      binop = BinOpExpr(clang::BO_Xor, type);
      break;

    case llvm::BinaryOperator::URem:
      rhs = IntSignCast(rhs, false);
      lhs = IntSignCast(lhs, false);
      binop = BinOpExpr(clang::BO_Rem, lhs->getType());
      break;

    case llvm::BinaryOperator::SRem:
      rhs = IntSignCast(rhs, true);
      lhs = IntSignCast(lhs, true);
      binop = BinOpExpr(clang::BO_Rem, lhs->getType());
      break;

    case llvm::BinaryOperator::UDiv:
      rhs = IntSignCast(rhs, false);
      lhs = IntSignCast(lhs, false);
      binop = BinOpExpr(clang::BO_Div, lhs->getType());
      break;

    case llvm::BinaryOperator::SDiv:
      rhs = IntSignCast(rhs, true);
      lhs = IntSignCast(lhs, true);
      binop = BinOpExpr(clang::BO_Div, lhs->getType());
      break;

    case llvm::BinaryOperator::FDiv:
      binop = BinOpExpr(clang::BO_Div, type);
      break;

    case llvm::BinaryOperator::Add:
    case llvm::BinaryOperator::FAdd:
      binop = BinOpExpr(clang::BO_Add, type);
      break;

    case llvm::BinaryOperator::Sub:
    case llvm::BinaryOperator::FSub:
      binop = BinOpExpr(clang::BO_Sub, type);
      break;

    case llvm::BinaryOperator::Mul:
    case llvm::BinaryOperator::FMul:
      binop = BinOpExpr(clang::BO_Mul, type);
      break;

    default:
      LOG(FATAL) << "Unknown BinaryOperator: " << inst.getOpcodeName();
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
  auto CmpExpr = [this, lhs, rhs](clang::BinaryOperatorKind opc) {
    return CreateBinaryOperator(ast_ctx, opc, CastOperand(rhs->getType(), lhs),
                                CastOperand(lhs->getType(), rhs),
                                ast_ctx.IntTy);
  };
  // Sign-cast int operand
  auto IntSignCast = [this](clang::Expr *operand, bool sign) {
    auto type = ast_ctx.getIntTypeForBitwidth(
        ast_ctx.getTypeSize(operand->getType()), sign);
    return CreateCStyleCastExpr(ast_ctx, type, clang::CastKind::CK_IntegralCast,
                                operand);
  };
  // Cast operands for signed predicates
  if (inst.isSigned()) {
    lhs = IntSignCast(lhs, true);
    rhs = IntSignCast(rhs, true);
  }
  // Cast operands for unsigned predicates
  if (inst.isUnsigned()) {
    lhs = IntSignCast(lhs, false);
    rhs = IntSignCast(rhs, false);
  }
  // Where the magic happens
  switch (inst.getPredicate()) {
    case llvm::CmpInst::ICMP_UGT:
    case llvm::CmpInst::ICMP_SGT:
    case llvm::CmpInst::FCMP_OGT:
      cmp = CmpExpr(clang::BO_GT);
      break;

    case llvm::CmpInst::ICMP_ULT:
    case llvm::CmpInst::ICMP_SLT:
    case llvm::CmpInst::FCMP_OLT:
      cmp = CmpExpr(clang::BO_LT);
      break;

    case llvm::CmpInst::ICMP_UGE:
    case llvm::CmpInst::ICMP_SGE:
    case llvm::CmpInst::FCMP_OGE:
      cmp = CmpExpr(clang::BO_GE);
      break;

    case llvm::CmpInst::ICMP_ULE:
    case llvm::CmpInst::ICMP_SLE:
    case llvm::CmpInst::FCMP_OLE:
      cmp = CmpExpr(clang::BO_LE);
      break;

    case llvm::CmpInst::ICMP_EQ:
    case llvm::CmpInst::FCMP_OEQ:
      cmp = CmpExpr(clang::BO_EQ);
      break;

    case llvm::CmpInst::ICMP_NE:
    case llvm::CmpInst::FCMP_UNE:
      cmp = CmpExpr(clang::BO_NE);
      break;

    default:
      LOG(FATAL) << "Unknown CmpInst predicate";
      break;
  }
}

void IRToASTVisitor::visitCastInst(llvm::CastInst &inst) {
  DLOG(INFO) << "visitCastInst: " << LLVMThingToString(&inst);
  auto &cast = stmts[&inst];
  if (cast) {
    return;
  }
  // Get cast operand
  auto operand = GetOperandExpr(inst.getOperand(0));
  // Get destination type
  auto type = GetQualType(inst.getType());
  // Convenience wrapper
  auto CastExpr = [this, &operand, &type](clang::CastKind opc) {
    return CreateCStyleCastExpr(ast_ctx, type, opc, operand);
  };
  // Create cast
  switch (inst.getOpcode()) {
    case llvm::CastInst::Trunc: {
      auto bitwidth = ast_ctx.getTypeSize(type);
      auto sign = operand->getType()->isSignedIntegerType();
      type = ast_ctx.getIntTypeForBitwidth(bitwidth, sign);
      cast = CastExpr(clang::CastKind::CK_IntegralCast);
    } break;

    case llvm::CastInst::BitCast:
      cast = CastExpr(clang::CastKind::CK_BitCast);
      break;

    case llvm::CastInst::SExt: {
      auto bitwidth = ast_ctx.getTypeSize(type);
      type = ast_ctx.getIntTypeForBitwidth(bitwidth, /*signed=*/1);
      cast = CastExpr(clang::CastKind::CK_IntegralCast);
    } break;

    case llvm::CastInst::ZExt:
      cast = CastExpr(clang::CastKind::CK_IntegralCast);
      break;

    case llvm::CastInst::PtrToInt:
      cast = CastExpr(clang::CastKind::CK_PointerToIntegral);
      break;

    case llvm::CastInst::IntToPtr:
      cast = CastExpr(clang::CastKind::CK_IntegralToPointer);
      break;

    case llvm::CastInst::SIToFP:
      cast = CastExpr(clang::CastKind::CK_IntegralToFloating);
      break;

    case llvm::CastInst::FPToUI:
    case llvm::CastInst::FPToSI:
      cast = CastExpr(clang::CastKind::CK_FloatingToIntegral);
      break;

    case llvm::CastInst::FPExt:
    case llvm::CastInst::FPTrunc:
      cast = CastExpr(clang::CastKind::CK_FloatingCast);
      break;

    default:
      LOG(FATAL) << "Unknown CastInst cast type";
      break;
  }
}

void IRToASTVisitor::visitSelectInst(llvm::SelectInst &inst) {
  DLOG(INFO) << "visitCastInst: " << LLVMThingToString(&inst);
  auto &select = stmts[&inst];
  if (select) {
    return;
  }

  auto cond = GetOperandExpr(inst.getCondition());
  auto tval = GetOperandExpr(inst.getTrueValue());
  auto fval = GetOperandExpr(inst.getFalseValue());
  auto type = GetQualType(inst.getType());

  select = CreateConditionalOperatorExpr(ast_ctx, cond, tval, fval, type);
}

void IRToASTVisitor::visitPHINode(llvm::PHINode &inst) {
  DLOG(INFO) << "visitPHINode: " << LLVMThingToString(&inst);
  LOG(FATAL) << "Uninimplemented llvm::PHINode visitor!";
}

}  // namespace rellic
