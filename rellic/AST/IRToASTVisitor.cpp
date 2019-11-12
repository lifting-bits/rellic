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

#include <iterator>

namespace rellic {

namespace {

template <typename T>
static size_t GetNumDecls(clang::DeclContext *decl_ctx) {
  size_t result = 0;
  for (auto decl : decl_ctx->decls()) {
    if (clang::isa<T>(decl)) {
      ++result;
    }
  }
  return result;
}

}  // namespace

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
      result =
          size == 1 ? ast_ctx.BoolTy : ast_ctx.getIntTypeForBitwidth(size, 0);
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
        auto tudecl = ast_ctx.getTranslationUnitDecl();
        auto strct = llvm::cast<llvm::StructType>(type);
        auto sname = strct->getName().str();
        if (sname.empty()) {
          auto num = GetNumDecls<clang::TypeDecl>(tudecl);
          sname = "struct_" + std::to_string(num);
        }
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
    case llvm::Type::DoubleTyID: {
      auto val = llvm::cast<llvm::ConstantFP>(constant)->getValueAPF();
      result = CreateFloatingLiteral(ast_ctx, val, c_type);
    } break;
    // Integers
    case llvm::Type::IntegerTyID: {
      auto val = llvm::cast<llvm::ConstantInt>(constant)->getValue();
      switch (clang::cast<clang::BuiltinType>(c_type)->getKind()) {
        case clang::BuiltinType::Kind::UChar:
        case clang::BuiltinType::Kind::SChar:
          result =
              CreateCharacterLiteral(ast_ctx, val.getLimitedValue(), c_type);
          break;

        case clang::BuiltinType::Kind::Short:
          result = CreateIntegerLiteral(ast_ctx, val, ast_ctx.IntTy);
          break;

        case clang::BuiltinType::Kind::Bool:
        case clang::BuiltinType::Kind::UShort:
          result = CreateIntegerLiteral(ast_ctx, val, ast_ctx.UnsignedIntTy);
          break;

        case clang::BuiltinType::Kind::Int:
        case clang::BuiltinType::Kind::UInt:
        case clang::BuiltinType::Kind::Long:
        case clang::BuiltinType::Kind::ULong:
        case clang::BuiltinType::Kind::LongLong:
        case clang::BuiltinType::Kind::ULongLong:
          result = CreateIntegerLiteral(ast_ctx, val, c_type);
          break;

        default:
          LOG(FATAL) << "Unsupported integer literal type";
          break;
      }
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

  auto type = llvm::cast<llvm::PointerType>(gvar.getType())->getElementType();
  auto tudecl = ast_ctx.getTranslationUnitDecl();
  auto name = gvar.getName().str();
  if (name.empty()) {
    auto num = GetNumDecls<clang::VarDecl>(tudecl);
    name = "gvar" + std::to_string(num);
  }
  // Create a variable declaration
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

  auto type = GetQualType(inst.getType());

  std::vector<clang::Expr *> args;
  for (auto &arg : inst.arg_operands()) {
    auto expr = GetOperandExpr(arg);
    auto type = expr->getType();
    auto cast =
        CreateImplicitCastExpr(ast_ctx, type, clang::CK_LValueToRValue, expr);
    args.push_back(cast);
  }

  auto callee = inst.getCalledValue();
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
        auto type = llvm::cast<llvm::PointerType>(indexed_type);
        indexed_type = type->getElementType();
      } break;
      // Arrays
      case llvm::Type::ArrayTyID: {
        base = CreateImplicitCastExpr(
            ast_ctx, ast_ctx.getArrayDecayedType(base->getType()),
            clang::CK_ArrayToPointerDecay, base);
        IndexPtr(*idx);
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
    // Add parens to preserve expression semantics
    base = CreateParenExpr(ast_ctx, base);
  }

  ref = CreateUnaryOperator(ast_ctx, clang::UO_AddrOf, base,
                            ast_ctx.getPointerType(base->getType()));
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
                                    clang::QualType res_type) {
    return CreateBinaryOperator(ast_ctx, opc, CastOperand(rhs->getType(), lhs),
                                CastOperand(lhs->getType(), rhs), res_type);
  };
  // Get result type
  auto type = GetQualType(inst.getType());
  // Where the magic happens
  switch (inst.getOpcode()) {
    case llvm::BinaryOperator::LShr: {
      auto sign = ast_ctx.getIntTypeForBitwidth(
          ast_ctx.getTypeSize(lhs->getType()), /*signed=*/0);
      lhs = CreateCStyleCastExpr(ast_ctx, sign,
                                 clang::CastKind::CK_IntegralCast, lhs);
      binop = BinOpExpr(clang::BO_Shr, type);
    } break;

    case llvm::BinaryOperator::AShr: {
      auto sign = ast_ctx.getIntTypeForBitwidth(
          ast_ctx.getTypeSize(lhs->getType()), /*signed=*/1);
      lhs = CreateCStyleCastExpr(ast_ctx, sign,
                                 clang::CastKind::CK_IntegralCast, lhs);
      binop = BinOpExpr(clang::BO_Shr, type);
    } break;

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
      binop = BinOpExpr(clang::BO_Rem, lhs->getType());
      break;

    case llvm::BinaryOperator::SRem:
      binop = BinOpExpr(clang::BO_Rem, lhs->getType());
      break;

    case llvm::BinaryOperator::Add:
      binop = BinOpExpr(clang::BO_Add, type);
      break;

    case llvm::BinaryOperator::Sub:
      binop = BinOpExpr(clang::BO_Sub, type);
      break;

    case llvm::BinaryOperator::Mul:
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
                                ast_ctx.BoolTy);
  };
  // Where the magic happens
  switch (inst.getPredicate()) {
    case llvm::CmpInst::ICMP_UGT:
      cmp = CmpExpr(clang::BO_GT);
      break;

    case llvm::CmpInst::ICMP_ULT:
      cmp = CmpExpr(clang::BO_LT);
      break;

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
