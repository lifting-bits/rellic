/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#define GOOGLE_STRIP_LOG 1

#include "rellic/AST/IRToASTVisitor.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <iterator>

#include "rellic/AST/Util.h"
#include "rellic/BC/Compat/IntrinsicInst.h"
#include "rellic/BC/Compat/Value.h"
#include "rellic/BC/Util.h"

namespace rellic {

IRToASTVisitor::IRToASTVisitor(clang::ASTUnit &unit)
    : ast_ctx(unit.getASTContext()), ast(unit) {}

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
      auto size{type->getIntegerBitWidth()};
      CHECK(size > 0) << "Integer bit width has to be greater than 0";
      result = ast.GetLeastIntTypeForBitWidth(size, /*sign=*/0);
    } break;

    case llvm::Type::FunctionTyID: {
      auto func{llvm::cast<llvm::FunctionType>(type)};
      auto ret{GetQualType(func->getReturnType())};
      std::vector<clang::QualType> params;
      for (auto param : func->params()) {
        params.push_back(GetQualType(param));
      }
      auto epi{clang::FunctionProtoType::ExtProtoInfo()};
      epi.Variadic = params.size() > 0U && func->isVarArg();
      result = ast_ctx.getFunctionType(ret, params, epi);
    } break;

    case llvm::Type::PointerTyID: {
      auto ptr{llvm::cast<llvm::PointerType>(type)};
      result = ast_ctx.getPointerType(GetQualType(ptr->getElementType()));
    } break;

    case llvm::Type::ArrayTyID: {
      auto arr{llvm::cast<llvm::ArrayType>(type)};
      auto elm{GetQualType(arr->getElementType())};
      result = GetConstantArrayType(ast_ctx, elm, arr->getNumElements());
    } break;

    case llvm::Type::StructTyID: {
      clang::RecordDecl *sdecl{nullptr};
      auto &decl{type_decls[type]};
      if (!decl) {
        auto tudecl{ast_ctx.getTranslationUnitDecl()};
        auto strct{llvm::cast<llvm::StructType>(type)};
        auto sname{strct->getName().str()};
        if (sname.empty()) {
          auto num{GetNumDecls<clang::TypeDecl>(tudecl)};
          sname = "struct" + std::to_string(num);
        }
        // Create a C struct declaration
        decl = sdecl = ast.CreateStructDecl(tudecl, sname);
        // Add fields to the C struct
        for (auto ecnt{0U}; ecnt < strct->getNumElements(); ++ecnt) {
          auto etype{GetQualType(strct->getElementType(ecnt))};
          auto fname{"field" + std::to_string(ecnt)};
          sdecl->addDecl(ast.CreateFieldDecl(sdecl, etype, fname));
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

    default: {
      if (type->isVectorTy()) {
        auto vtype{llvm::cast<llvm::FixedVectorType>(type)};
        auto etype{GetQualType(vtype->getElementType())};
        auto ecnt{vtype->getNumElements()};
        auto vkind{clang::VectorType::GenericVector};
        result = ast_ctx.getVectorType(etype, ecnt, vkind);
      } else {
        LOG(FATAL) << "Unknown LLVM Type: " << LLVMThingToString(type);
      }
    } break;
  }

  CHECK(!result.isNull()) << "Unknown LLVM Type";

  return result;
}

clang::Expr *IRToASTVisitor::CreateLiteralExpr(llvm::Constant *constant) {
  DLOG(INFO) << "Creating literal Expr for " << LLVMThingToString(constant);

  clang::Expr *result{nullptr};

  auto l_type{constant->getType()};
  auto c_type{GetQualType(l_type)};

  auto CreateInitListLiteral{[this, &constant] {
    std::vector<clang::Expr *> init_exprs;
    if (!constant->isZeroValue()) {
      for (auto i{0U}; auto elm = constant->getAggregateElement(i); ++i) {
        init_exprs.push_back(GetOperandExpr(elm));
      }
    }
    return ast.CreateInitList(init_exprs);
  }};

  switch (l_type->getTypeID()) {
    // Floats
    case llvm::Type::HalfTyID:
    case llvm::Type::FloatTyID:
    case llvm::Type::DoubleTyID:
    case llvm::Type::X86_FP80TyID: {
      result = ast.CreateFPLit(
          llvm::cast<llvm::ConstantFP>(constant)->getValueAPF());
    } break;
    // Integers
    case llvm::Type::IntegerTyID: {
      if (llvm::isa<llvm::ConstantInt>(constant)) {
        auto ci{llvm::cast<llvm::ConstantInt>(constant)};
        auto val{ci->getValue()};
        if (val.getBitWidth() == 1U) {
          result = ast.CreateIntLit(val);
        } else {
          result = ast.CreateAdjustedIntLit(val);
        }
      } else if (llvm::isa<llvm::UndefValue>(constant)) {
        result = ast.CreateUndefInteger(c_type);
      } else {
        LOG(FATAL) << "Unsupported integer constant";
      }
    } break;
    // Pointers
    case llvm::Type::PointerTyID: {
      if (llvm::isa<llvm::ConstantPointerNull>(constant)) {
        result = ast.CreateNull();
      } else if (llvm::isa<llvm::UndefValue>(constant)) {
        result = ast.CreateUndefPointer(c_type);
      } else {
        LOG(FATAL) << "Unsupported pointer constant";
      }
    } break;
    // Arrays
    case llvm::Type::ArrayTyID: {
      auto elm_type{llvm::cast<llvm::ArrayType>(l_type)->getElementType()};
      if (elm_type->isIntegerTy(8U)) {
        std::string init{""};
        if (auto arr = llvm::dyn_cast<llvm::ConstantDataArray>(constant)) {
          init = arr->getAsString().str();
        }
        result = ast.CreateStrLit(init);
      } else {
        result = CreateInitListLiteral();
      }
    } break;
    // Structures
    case llvm::Type::StructTyID:
      result = CreateInitListLiteral();
      break;

    default: {
      // Vectors
      if (l_type->isVectorTy()) {
        result = ast.CreateCompoundLit(c_type, CreateInitListLiteral());
      } else {
        LOG(FATAL) << "Unknown LLVM constant type: "
                   << LLVMThingToString(l_type);
      }
    } break;
  }

  return result;
}

#define ASSERT_ON_VALUE_TYPE(x)               \
  if (llvm::isa<x>(val)) {                    \
    LOG(FATAL) << "Invalid operand [" #x "]"; \
  }

clang::Expr *IRToASTVisitor::GetOperandExpr(llvm::Value *val) {
  DLOG(INFO) << "Getting Expr for " << LLVMThingToString(val);
  // Helper functions
  auto CreateExpr{[this, &val] {
    auto stmt{GetOrCreateStmt(val)};
    return clang::cast<clang::Expr>(stmt);
  }};

  auto CreateRef{[this, &val] {
    auto decl{GetOrCreateDecl(val)};
    return ast.CreateDeclRef(clang::cast<clang::ValueDecl>(decl));
  }};
  // Operand is a constant value
  if (llvm::isa<llvm::ConstantExpr>(val) ||
      llvm::isa<llvm::ConstantAggregate>(val) ||
      llvm::isa<llvm::ConstantData>(val)) {
    return CreateExpr();
  }
  // Operand is an l-value (variable, function, ...)
  if (llvm::isa<llvm::GlobalValue>(val) || llvm::isa<llvm::AllocaInst>(val)) {
    // Add a `&` operator
    return ast.CreateAddrOf(CreateRef());
  }
  // Operand is a function argument or local variable
  if (llvm::isa<llvm::Argument>(val)) {
    return CreateRef();
  }
  // Operand is a result of an expression
  if (auto inst = llvm::dyn_cast<llvm::Instruction>(val)) {
    // Handle calls to functions with a return value and side-effects
    if (llvm::isa<llvm::CallInst>(inst) && inst->mayHaveSideEffects() &&
        !inst->getType()->isVoidTy()) {
      auto assign{clang::cast<clang::BinaryOperator>(GetOrCreateStmt(val))};
      return assign->getLHS();
    }
    // Everything else
    return CreateExpr();
  }

  ASSERT_ON_VALUE_TYPE(llvm::MetadataAsValue);
  ASSERT_ON_VALUE_TYPE(llvm::Constant);
  ASSERT_ON_VALUE_TYPE(llvm::BasicBlock);
  ASSERT_ON_VALUE_TYPE(llvm::GlobalVariable);
  ASSERT_ON_VALUE_TYPE(llvm::GlobalAlias);
  ASSERT_ON_VALUE_TYPE(llvm::GlobalIFunc);
  ASSERT_ON_VALUE_TYPE(llvm::GlobalIndirectSymbol);
  ASSERT_ON_VALUE_TYPE(llvm::GlobalObject);
  ASSERT_ON_VALUE_TYPE(llvm::FPMathOperator);
  ASSERT_ON_VALUE_TYPE(llvm::Operator);
  ASSERT_ON_VALUE_TYPE(llvm::BlockAddress);

  LOG(FATAL) << "Invalid operand value id: [" << val->getValueID() << "]\n"
             << "Bitcode: [" << LLVMThingToString(val) << "]\n"
             << "Type: [" << LLVMThingToString(val->getType()) << "]\n";

  return nullptr;
}

clang::Decl *IRToASTVisitor::GetOrCreateIntrinsic(llvm::InlineAsm *val) {
  auto &decl{value_decls[val]};
  if (decl) {
    return decl;
  }

  auto tudecl{ast_ctx.getTranslationUnitDecl()};
  auto name{"asm_" + std::to_string(GetNumDecls<clang::FunctionDecl>(tudecl))};
  auto type{GetQualType(val->getType()->getPointerElementType())};
  decl = ast.CreateFunctionDecl(tudecl, type, name);

  return decl;
}

clang::Stmt *IRToASTVisitor::GetOrCreateStmt(llvm::Value *val) {
  auto &stmt{stmts[val]};
  if (stmt) {
    return stmt;
  }
  // ConstantExpr
  if (auto cexpr = llvm::dyn_cast<llvm::ConstantExpr>(val)) {
    auto inst{cexpr->getAsInstruction()};
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

  LOG(FATAL) << "Unsupported value type: " << LLVMThingToString(val);

  return stmt;
}

void IRToASTVisitor::SetStmt(llvm::Value *val, clang::Stmt *stmt) {
  stmts[val] = stmt;
}

clang::Decl *IRToASTVisitor::GetOrCreateDecl(llvm::Value *val) {
  auto &decl{value_decls[val]};
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
    LOG(FATAL) << "Unsupported value type: " << LLVMThingToString(val);
  }

  return decl;
}

void IRToASTVisitor::VisitGlobalVar(llvm::GlobalVariable &gvar) {
  DLOG(INFO) << "VisitGlobalVar: " << LLVMThingToString(&gvar);
  auto &var{value_decls[&gvar]};
  if (var) {
    return;
  }

  if (IsGlobalMetadata(gvar)) {
    DLOG(INFO) << "Skipping global variable only used for metadata";
    return;
  }

  auto type{llvm::cast<llvm::PointerType>(gvar.getType())->getElementType()};
  auto tudecl{ast_ctx.getTranslationUnitDecl()};
  auto name{gvar.getName().str()};
  if (name.empty()) {
    name = "gvar" + std::to_string(GetNumDecls<clang::VarDecl>(tudecl));
  }
  // Create a variable declaration
  var = ast.CreateVarDecl(tudecl, GetQualType(type), name);
  // Add to translation unit
  tudecl->addDecl(var);
  // Create an initalizer literal
  if (gvar.hasInitializer()) {
    clang::cast<clang::VarDecl>(var)->setInit(
        GetOperandExpr(gvar.getInitializer()));
  }
}

void IRToASTVisitor::VisitArgument(llvm::Argument &arg) {
  DLOG(INFO) << "VisitArgument: " << LLVMThingToString(&arg);
  auto &parm{value_decls[&arg]};
  if (parm) {
    return;
  }
  // Create a name
  auto name{arg.hasName() ? arg.getName().str()
                          : "arg" + std::to_string(arg.getArgNo())};
  // Get parent function declaration
  auto func{arg.getParent()};
  auto fdecl{clang::cast<clang::FunctionDecl>(GetOrCreateDecl(func))};
  // Create a declaration
  parm = ast.CreateParamDecl(fdecl, GetQualType(arg.getType()), name);
}

void IRToASTVisitor::VisitFunctionDecl(llvm::Function &func) {
  auto name{func.getName().str()};
  DLOG(INFO) << "VisitFunctionDecl: " << name;

  if (IsAnnotationIntrinsic(func.getIntrinsicID())) {
    DLOG(INFO) << "Skipping creating declaration for LLVM intrinsic";
    return;
  }

  auto &decl{value_decls[&func]};
  if (decl) {
    return;
  }

  DLOG(INFO) << "Creating FunctionDecl for " << name;
  auto tudecl{ast_ctx.getTranslationUnitDecl()};
  auto type{GetQualType(func.getFunctionType())};
  decl = ast.CreateFunctionDecl(tudecl, type, name);

  tudecl->addDecl(decl);

  if (func.arg_empty()) {
    return;
  }

  std::vector<clang::ParmVarDecl *> params;
  for (auto &arg : func.args()) {
    auto parm{clang::cast<clang::ParmVarDecl>(GetOrCreateDecl(&arg))};
    params.push_back(parm);
  }

  decl->getAsFunction()->setParams(params);
}

void IRToASTVisitor::visitIntrinsicInst(llvm::IntrinsicInst &inst) {
  DLOG(INFO) << "visitIntrinsicInst: " << LLVMThingToString(&inst);

  // NOTE(artem): As of this writing, rellic does not do anything
  // with debug intrinsics and debug metadata. Processing them to C
  // is useless (since there is no C equivalent anyway).
  // All it does it lead to triggering of asserts for things that are
  // low-priority on the "to fix" list

  if (llvm::isDbgInfoIntrinsic(inst.getIntrinsicID())) {
    DLOG(INFO) << "Skipping debug data intrinsic";
    return;
  }

  if (IsAnnotationIntrinsic(inst.getIntrinsicID())) {
    // Some of this overlaps with the debug data case above.
    // This is fine. We want debug data special cased as we know it is present
    // and we may make use of it earlier than other annotations
    DLOG(INFO) << "Skipping non-debug annotation";
    return;
  }

  // handle this as a CallInst, which IntrinsicInst derives from
  return visitCallInst(inst);
}

void IRToASTVisitor::visitCallInst(llvm::CallInst &inst) {
  DLOG(INFO) << "visitCallInst: " << LLVMThingToString(&inst);
  auto &callstmt{stmts[&inst]};
  if (callstmt) {
    return;
  }

  std::vector<clang::Expr *> args;
  for (auto &arg : inst.arg_operands()) {
    args.push_back(GetOperandExpr(arg));
  }

  clang::Expr *callexpr{nullptr};
  auto callee{inst.getCalledOperand()};
  if (auto func = llvm::dyn_cast<llvm::Function>(callee)) {
    auto fdecl{GetOrCreateDecl(func)->getAsFunction()};
    callexpr = ast.CreateCall(fdecl, args);
  } else if (auto iasm = llvm::dyn_cast<llvm::InlineAsm>(callee)) {
    auto fdecl{GetOrCreateIntrinsic(iasm)->getAsFunction()};
    callexpr = ast.CreateCall(fdecl, args);
  } else if (llvm::isa<llvm::PointerType>(callee->getType())) {
    callexpr = ast.CreateCall(GetOperandExpr(callee), args);
  } else {
    LOG(FATAL) << "Callee is not a function";
  }

  if (inst.mayHaveSideEffects() && !inst.getType()->isVoidTy()) {
    auto fdecl{GetOrCreateDecl(inst.getFunction())->getAsFunction()};
    auto name{"val" + std::to_string(GetNumDecls<clang::VarDecl>(fdecl))};
    auto var{ast.CreateVarDecl(fdecl, callexpr->getType(), name)};
    fdecl->addDecl(var);
    callstmt = ast.CreateAssign(ast.CreateDeclRef(var), callexpr);
  } else {
    callstmt = callexpr;
  }
}

void IRToASTVisitor::visitGetElementPtrInst(llvm::GetElementPtrInst &inst) {
  DLOG(INFO) << "visitGetElementPtrInst: " << LLVMThingToString(&inst);
  auto &ref{stmts[&inst]};
  if (ref) {
    return;
  }

  auto indexed_type{inst.getPointerOperandType()};
  auto base{GetOperandExpr(inst.getPointerOperand())};

  for (auto &idx : llvm::make_range(inst.idx_begin(), inst.idx_end())) {
    switch (indexed_type->getTypeID()) {
      // Initial pointer
      case llvm::Type::PointerTyID: {
        CHECK(idx == *inst.idx_begin())
            << "Indexing an llvm::PointerType is only valid at first index";
        base = ast.CreateArraySub(base, GetOperandExpr(idx));
        indexed_type =
            llvm::cast<llvm::PointerType>(indexed_type)->getElementType();
      } break;
      // Arrays
      case llvm::Type::ArrayTyID: {
        base = ast.CreateArraySub(base, GetOperandExpr(idx));
        indexed_type =
            llvm::cast<llvm::ArrayType>(indexed_type)->getElementType();
      } break;
      // Vectors
      case llvm::Type::FixedVectorTyID: {
        auto l_vec_ty{llvm::cast<llvm::VectorType>(indexed_type)};
        auto l_elm_ty{l_vec_ty->getElementType()};
        auto c_elm_ty{GetQualType(l_elm_ty)};
        base = ast.CreateCStyleCast(ast_ctx.getPointerType(c_elm_ty),
                                    ast.CreateAddrOf(base));
        base = ast.CreateArraySub(base, GetOperandExpr(idx));
        indexed_type = l_elm_ty;
      } break;
      // Structures
      case llvm::Type::StructTyID: {
        auto mem_idx = llvm::dyn_cast<llvm::ConstantInt>(idx);
        CHECK(mem_idx) << "Non-constant GEP index while indexing a structure";
        auto tdecl{type_decls[indexed_type]};
        CHECK(tdecl) << "Structure declaration doesn't exist";
        auto record{clang::cast<clang::RecordDecl>(tdecl)};
        auto field_it{record->field_begin()};
        std::advance(field_it, mem_idx->getLimitedValue());
        CHECK(field_it != record->field_end()) << "GEP index is out of bounds";
        base = ast.CreateDot(base, *field_it);
        indexed_type =
            llvm::cast<llvm::StructType>(indexed_type)->getTypeAtIndex(idx);
      } break;
      // Unknown
      default:
        LOG(FATAL) << "Indexing an unknown type: "
                   << LLVMThingToString(indexed_type);
        break;
    }
  }

  ref = ast.CreateAddrOf(base);
}

void IRToASTVisitor::visitExtractValueInst(llvm::ExtractValueInst &inst) {
  DLOG(INFO) << "visitExtractValueInst: " << LLVMThingToString(&inst);
  auto &ref{stmts[&inst]};
  if (ref) {
    return;
  }

  auto base{GetOperandExpr(inst.getAggregateOperand())};
  auto indexed_type{inst.getAggregateOperand()->getType()};

  for (auto idx : llvm::make_range(inst.idx_begin(), inst.idx_end())) {
    switch (indexed_type->getTypeID()) {
      // Arrays
      case llvm::Type::ArrayTyID: {
        base = ast.CreateArraySub(
            base, ast.CreateIntLit(llvm::APInt(sizeof(unsigned) * 8U, idx)));
        indexed_type =
            llvm::cast<llvm::ArrayType>(indexed_type)->getElementType();
      } break;
      // Structures
      case llvm::Type::StructTyID: {
        auto tdecl{type_decls[indexed_type]};
        CHECK(tdecl) << "Structure declaration doesn't exist";
        auto record{clang::cast<clang::RecordDecl>(tdecl)};
        auto field_it{record->field_begin()};
        std::advance(field_it, idx);
        CHECK(field_it != record->field_end())
            << "ExtractValue index is out of bounds";
        base = ast.CreateDot(base, *field_it);
        indexed_type =
            llvm::cast<llvm::StructType>(indexed_type)->getTypeAtIndex(idx);
      } break;

      default:
        LOG(FATAL) << "Indexing an unknown aggregate type";
        break;
    }
  }

  ref = base;
}

void IRToASTVisitor::visitAllocaInst(llvm::AllocaInst &inst) {
  DLOG(INFO) << "visitAllocaInst: " << LLVMThingToString(&inst);
  auto &declref{stmts[&inst]};
  if (declref) {
    return;
  }

  auto func{inst.getFunction()};
  CHECK(func) << "AllocaInst does not have a parent function";

  auto &var{value_decls[&inst]};
  if (!var) {
    auto fdecl{clang::cast<clang::FunctionDecl>(GetOrCreateDecl(func))};
    auto name{inst.getName().str()};
    if (name.empty()) {
      name = "var" + std::to_string(GetNumDecls<clang::VarDecl>(fdecl));
    }
    var = ast.CreateVarDecl(fdecl, GetQualType(inst.getAllocatedType()), name);
    fdecl->addDecl(var);
  }

  declref = ast.CreateDeclRef(var);
}

void IRToASTVisitor::visitStoreInst(llvm::StoreInst &inst) {
  DLOG(INFO) << "visitStoreInst: " << LLVMThingToString(&inst);
  auto &assign{stmts[&inst]};
  if (assign) {
    return;
  }
  // Stores in LLVM IR correspond to value assignments in C
  // Get the operand we're assigning to
  auto lhs{GetOperandExpr(inst.getPointerOperand())};
  // Get the operand we're assigning from
  auto rhs{GetOperandExpr(inst.getValueOperand())};
  // Create the assignemnt itself
  assign = ast.CreateAssign(ast.CreateDeref(lhs), rhs);
}

void IRToASTVisitor::visitLoadInst(llvm::LoadInst &inst) {
  DLOG(INFO) << "visitLoadInst: " << LLVMThingToString(&inst);
  auto &ref{stmts[&inst]};
  if (ref) {
    return;
  }
  ref = ast.CreateDeref(GetOperandExpr(inst.getPointerOperand()));
}

void IRToASTVisitor::visitReturnInst(llvm::ReturnInst &inst) {
  DLOG(INFO) << "visitReturnInst: " << LLVMThingToString(&inst);
  auto &retstmt{stmts[&inst]};
  if (retstmt) {
    return;
  }

  if (auto retval = inst.getReturnValue()) {
    retstmt = ast.CreateReturn(GetOperandExpr(retval));
  } else {
    retstmt = ast.CreateReturn();
  }
}

void IRToASTVisitor::visitBinaryOperator(llvm::BinaryOperator &inst) {
  DLOG(INFO) << "visitBinaryOperator: " << LLVMThingToString(&inst);
  auto &binop{stmts[&inst]};
  if (binop) {
    return;
  }
  // Get operands
  auto lhs{GetOperandExpr(inst.getOperand(0))};
  auto rhs{GetOperandExpr(inst.getOperand(1))};
  // Sign-cast int operand
  auto IntSignCast{[this](clang::Expr *operand, bool sign) {
    auto type{ast_ctx.getIntTypeForBitwidth(
        ast_ctx.getTypeSize(operand->getType()), sign)};
    return ast.CreateCStyleCast(type, operand);
  }};
  // Where the magic happens
  switch (inst.getOpcode()) {
    case llvm::BinaryOperator::LShr:
      binop = ast.CreateShr(IntSignCast(lhs, false), rhs);
      break;

    case llvm::BinaryOperator::AShr:
      binop = ast.CreateShr(IntSignCast(lhs, true), rhs);
      break;

    case llvm::BinaryOperator::Shl:
      binop = ast.CreateShl(lhs, rhs);
      break;

    case llvm::BinaryOperator::And:
      binop = inst.getType()->isIntegerTy(1U) ? ast.CreateLAnd(lhs, rhs)
                                              : ast.CreateAnd(lhs, rhs);
      break;

    case llvm::BinaryOperator::Or:
      binop = inst.getType()->isIntegerTy(1U) ? ast.CreateLOr(lhs, rhs)
                                              : ast.CreateOr(lhs, rhs);
      break;

    case llvm::BinaryOperator::Xor:
      binop = ast.CreateXor(lhs, rhs);
      break;

    case llvm::BinaryOperator::URem:
      binop = ast.CreateRem(IntSignCast(lhs, false), IntSignCast(rhs, false));
      break;

    case llvm::BinaryOperator::SRem:
      binop = ast.CreateRem(IntSignCast(lhs, true), IntSignCast(rhs, true));
      break;

    case llvm::BinaryOperator::UDiv:
      binop = ast.CreateDiv(IntSignCast(lhs, false), IntSignCast(rhs, false));
      break;

    case llvm::BinaryOperator::SDiv:
      binop = ast.CreateDiv(IntSignCast(lhs, true), IntSignCast(rhs, true));
      break;

    case llvm::BinaryOperator::FDiv:
      binop = ast.CreateDiv(lhs, rhs);
      break;

    case llvm::BinaryOperator::Add:
    case llvm::BinaryOperator::FAdd:
      binop = ast.CreateAdd(lhs, rhs);
      break;

    case llvm::BinaryOperator::Sub:
    case llvm::BinaryOperator::FSub:
      binop = ast.CreateSub(lhs, rhs);
      break;

    case llvm::BinaryOperator::Mul:
    case llvm::BinaryOperator::FMul:
      binop = ast.CreateMul(lhs, rhs);
      break;

    default:
      LOG(FATAL) << "Unknown BinaryOperator: " << inst.getOpcodeName();
      break;
  }
}

void IRToASTVisitor::visitCmpInst(llvm::CmpInst &inst) {
  DLOG(INFO) << "visitCmpInst: " << LLVMThingToString(&inst);
  auto &cmp{stmts[&inst]};
  if (cmp) {
    return;
  }
  // Get operands
  auto lhs{GetOperandExpr(inst.getOperand(0))};
  auto rhs{GetOperandExpr(inst.getOperand(1))};
  // Sign-cast int operand
  auto IntSignCast{[this](clang::Expr *op, bool sign) {
    auto ot{op->getType()};
    auto rt{ast_ctx.getIntTypeForBitwidth(ast_ctx.getTypeSize(ot), sign)};
    return rt == ot ? op : ast.CreateCStyleCast(rt, op);
  }};
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
      cmp = ast.CreateGT(lhs, rhs);
      break;

    case llvm::CmpInst::ICMP_ULT:
    case llvm::CmpInst::ICMP_SLT:
    case llvm::CmpInst::FCMP_OLT:
      cmp = ast.CreateLT(lhs, rhs);
      break;

    case llvm::CmpInst::ICMP_UGE:
    case llvm::CmpInst::ICMP_SGE:
    case llvm::CmpInst::FCMP_OGE:
      cmp = ast.CreateGE(lhs, rhs);
      break;

    case llvm::CmpInst::ICMP_ULE:
    case llvm::CmpInst::ICMP_SLE:
    case llvm::CmpInst::FCMP_OLE:
      cmp = ast.CreateLE(lhs, rhs);
      break;

    case llvm::CmpInst::ICMP_EQ:
    case llvm::CmpInst::FCMP_OEQ:
      cmp = ast.CreateEQ(lhs, rhs);
      break;

    case llvm::CmpInst::ICMP_NE:
    case llvm::CmpInst::FCMP_UNE:
      cmp = ast.CreateNE(lhs, rhs);
      break;

    default:
      LOG(FATAL) << "Unknown CmpInst predicate: " << inst.getOpcodeName();
      break;
  }
}

void IRToASTVisitor::visitCastInst(llvm::CastInst &inst) {
  DLOG(INFO) << "visitCastInst: " << LLVMThingToString(&inst);
  auto &cast{stmts[&inst]};
  if (cast) {
    return;
  }
  // There should always be an operand with a cast instruction
  // Get a C-language expression of the operand
  auto operand{GetOperandExpr(inst.getOperand(0))};
  // Get destination type
  auto type{GetQualType(inst.getType())};
  // Adjust type
  switch (inst.getOpcode()) {
    case llvm::CastInst::Trunc: {
      auto bitwidth{ast_ctx.getTypeSize(type)};
      auto sign{operand->getType()->isSignedIntegerType()};
      type = ast_ctx.getIntTypeForBitwidth(bitwidth, sign);
    } break;

    case llvm::CastInst::SExt: {
      auto bitwidth{ast_ctx.getTypeSize(type)};
      type = ast_ctx.getIntTypeForBitwidth(bitwidth, /*signed=*/1);
    } break;

    case llvm::CastInst::AddrSpaceCast:
      // NOTE(artem): ignore addrspace casts for now, but eventually we want to
      // handle them as __thread or some other context-relative handler
      // which will *highly* depend on the target architecture
      DLOG(WARNING)
          << __FUNCTION__
          << ": Ignoring an AddrSpaceCast because it is not yet implemented.";
      // The fallthrough is intentional here
      [[clang::fallthrough]];

    case llvm::CastInst::ZExt:
    case llvm::CastInst::BitCast:
    case llvm::CastInst::PtrToInt:
    case llvm::CastInst::IntToPtr:
    case llvm::CastInst::SIToFP:
    case llvm::CastInst::FPToUI:
    case llvm::CastInst::FPToSI:
    case llvm::CastInst::FPExt:
    case llvm::CastInst::FPTrunc:
      break;

    default:
      LOG(FATAL) << "Unknown CastInst cast type";
      break;
  }
  // Create cast
  cast = ast.CreateCStyleCast(type, operand);
}

void IRToASTVisitor::visitSelectInst(llvm::SelectInst &inst) {
  DLOG(INFO) << "visitSelectInst: " << LLVMThingToString(&inst);
  auto &select{stmts[&inst]};
  if (select) {
    return;
  }

  auto cond{GetOperandExpr(inst.getCondition())};
  auto tval{GetOperandExpr(inst.getTrueValue())};
  auto fval{GetOperandExpr(inst.getFalseValue())};

  select = ast.CreateConditional(cond, tval, fval);
}

void IRToASTVisitor::visitPHINode(llvm::PHINode &inst) {
  DLOG(INFO) << "visitPHINode: " << LLVMThingToString(&inst);
  LOG(FATAL) << "Unexpected llvm::PHINode. Try running llvm's reg2mem pass "
                "before decompiling.";
}

}  // namespace rellic
