/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include <clang/Basic/Builtins.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>

#include <vector>
#define GOOGLE_STRIP_LOG 1

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <iterator>

#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/BC/Compat/DerivedTypes.h"
#include "rellic/BC/Compat/IntrinsicInst.h"
#include "rellic/BC/Compat/Value.h"
#include "rellic/BC/Util.h"
#include "rellic/Exception.h"

namespace rellic {
class ExprGen : public llvm::InstVisitor<ExprGen, clang::Expr *> {
 private:
  clang::ASTContext &ast_ctx;

  ASTBuilder ast;

  Provenance &provenance;
  size_t num_literal_structs = 0;
  size_t num_declared_structs = 0;

 public:
  ExprGen(clang::ASTUnit &unit, Provenance &provenance)
      : ast_ctx(unit.getASTContext()), ast(unit), provenance(provenance) {}

  void VisitGlobalVar(llvm::GlobalVariable &gvar);
  clang::QualType GetQualType(llvm::Type *type);

  clang::Expr *CreateConstantExpr(llvm::Constant *constant);
  clang::Expr *CreateLiteralExpr(llvm::Constant *constant);
  clang::Expr *CreateOperandExpr(llvm::Use &val);

  clang::Expr *visitMemCpyInst(llvm::MemCpyInst &inst);
  clang::Expr *visitMemCpyInlineInst(llvm::MemCpyInlineInst &inst);
  clang::Expr *visitAnyMemMoveInst(llvm::AnyMemMoveInst &inst);
  clang::Expr *visitAnyMemSetInst(llvm::AnyMemSetInst &inst);
  clang::Expr *visitIntrinsicInst(llvm::IntrinsicInst &inst);
  clang::Expr *visitCallInst(llvm::CallInst &inst);
  clang::Expr *visitGetElementPtrInst(llvm::GetElementPtrInst &inst);
  clang::Expr *visitInstruction(llvm::Instruction &inst);
  clang::Expr *visitExtractValueInst(llvm::ExtractValueInst &inst);
  clang::Expr *visitLoadInst(llvm::LoadInst &inst);
  clang::Expr *visitBinaryOperator(llvm::BinaryOperator &inst);
  clang::Expr *visitCmpInst(llvm::CmpInst &inst);
  clang::Expr *visitCastInst(llvm::CastInst &inst);
  clang::Expr *visitSelectInst(llvm::SelectInst &inst);
  clang::Expr *visitFreezeInst(llvm::FreezeInst &inst);
  clang::Expr *visitUnaryOperator(llvm::UnaryOperator &inst);
};

void ExprGen::VisitGlobalVar(llvm::GlobalVariable &gvar) {
  DLOG(INFO) << "VisitGlobalVar: " << LLVMThingToString(&gvar);
  auto &var{provenance.value_decls[&gvar]};
  if (var) {
    return;
  }

  clang::Expr *init{nullptr};
  // Create an initalizer literal
  if (gvar.hasInitializer()) {
    init = CreateConstantExpr(gvar.getInitializer());
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
  if (init) {
    clang::cast<clang::VarDecl>(var)->setInit(init);
  }
}

clang::QualType ExprGen::GetQualType(llvm::Type *type) {
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
      epi.Variadic = func->isVarArg();
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
      auto &decl{provenance.type_decls[type]};
      if (!decl) {
        auto tudecl{ast_ctx.getTranslationUnitDecl()};
        auto strct{llvm::cast<llvm::StructType>(type)};
        auto sname{strct->isLiteral() ? ("literal_struct_" +
                                         std::to_string(num_literal_structs++))
                                      : strct->getName().str()};
        if (sname.empty()) {
          sname = "struct" + std::to_string(num_declared_structs++);
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
        auto vtype{llvm::cast<llvm::VectorType>(type)};
        auto etype{GetQualType(vtype->getElementType())};
        auto ecnt{GetNumElements(vtype)};
        auto vkind{clang::VectorType::GenericVector};
        result = ast_ctx.getVectorType(etype, ecnt, vkind);
      } else {
        THROW() << "Unknown LLVM Type: " << LLVMThingToString(type);
      }
    } break;
  }

  CHECK_THROW(!result.isNull()) << "Unknown LLVM Type";

  return result;
}

clang::Expr *ExprGen::CreateConstantExpr(llvm::Constant *constant) {
  if (auto gvar = llvm::dyn_cast<llvm::GlobalVariable>(constant)) {
    VisitGlobalVar(*gvar);
  }

  if (auto cexpr = llvm::dyn_cast<llvm::ConstantExpr>(constant)) {
    auto inst{cexpr->getAsInstruction()};
    auto expr{visit(inst)};
    provenance.use_provenance.erase(expr);
    DeleteValue(inst);
    return expr;
  } else if (auto global = llvm::dyn_cast<llvm::GlobalValue>(constant)) {
    auto decl{provenance.value_decls[global]};
    auto ref{ast.CreateDeclRef(decl)};
    return ast.CreateAddrOf(ref);
  }
  return CreateLiteralExpr(constant);
}

clang::Expr *ExprGen::CreateLiteralExpr(llvm::Constant *constant) {
  DLOG(INFO) << "Creating literal Expr for " << LLVMThingToString(constant);

  clang::Expr *result{nullptr};

  auto l_type{constant->getType()};
  auto c_type{GetQualType(l_type)};

  auto CreateInitListLiteral{[this, &constant] {
    std::vector<clang::Expr *> init_exprs;
    if (!constant->isZeroValue()) {
      for (auto i{0U}; auto elm = constant->getAggregateElement(i); ++i) {
        init_exprs.push_back(CreateConstantExpr(elm));
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
        auto val{llvm::cast<llvm::ConstantInt>(constant)->getValue()};
        auto val_bitwidth{val.getBitWidth()};
        auto ull_bitwidth{ast_ctx.getIntWidth(ast_ctx.LongLongTy)};
        if (val_bitwidth == 1U) {
          // Booleans
          result = ast.CreateIntLit(val);
        } else if (val.getActiveBits() <= ull_bitwidth) {
          result = ast.CreateAdjustedIntLit(val);
        } else {
          // Values wider than `long long` will be represented as:
          // (uint128_t)hi_64 << 64U | lo_64
          auto lo{ast.CreateIntLit(val.extractBits(64U, 0U))};
          auto hi{ast.CreateIntLit(val.extractBits(val_bitwidth - 64U, 64U))};
          auto shl_val{ast.CreateIntLit(llvm::APInt(32U, 64U))};
          result = ast.CreateCStyleCast(ast_ctx.UnsignedInt128Ty, hi);
          result = ast.CreateShl(result, shl_val);
          result = ast.CreateOr(result, lo);
        }
      } else if (llvm::isa<llvm::UndefValue>(constant)) {
        result = ast.CreateUndefInteger(c_type);
      } else {
        THROW() << "Unsupported integer constant";
      }
    } break;
    // Pointers
    case llvm::Type::PointerTyID: {
      if (llvm::isa<llvm::ConstantPointerNull>(constant)) {
        result = ast.CreateNull();
      } else if (llvm::isa<llvm::UndefValue>(constant)) {
        result = ast.CreateUndefPointer(c_type);
      } else {
        THROW() << "Unsupported pointer constant";
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
        THROW() << "Unknown LLVM constant type: " << LLVMThingToString(l_type);
      }
    } break;
  }

  return result;
}

#define ASSERT_ON_VALUE_TYPE(x)               \
  if (llvm::isa<x>(val)) {                    \
    LOG(FATAL) << "Invalid operand [" #x "]"; \
  }

clang::Expr *ExprGen::CreateOperandExpr(llvm::Use &val) {
  DLOG(INFO) << "Getting Expr for " << LLVMThingToString(val);
  auto CreateRef{[this, &val] {
    auto decl{provenance.value_decls[val]};
    auto ref{ast.CreateDeclRef(decl)};
    provenance.use_provenance[ref] = &val;
    return ref;
  }};

  clang::Expr *res{nullptr};
  if (auto constant = llvm::dyn_cast<llvm::Constant>(val)) {
    // Operand is a constant value
    res = CreateConstantExpr(constant);
  } else if (llvm::isa<llvm::AllocaInst>(val)) {
    // Operand is an l-value (variable, function, ...)
    // Add a `&` operator
    res = ast.CreateAddrOf(CreateRef());
  } else if (llvm::isa<llvm::Argument>(val)) {
    // Operand is a function argument or local variable
    auto arg{llvm::cast<llvm::Argument>(val)};
    auto ref{CreateRef()};
    if (arg->hasByValAttr()) {
      // Since arguments that have the `byval` are pointers, but actually mean
      // pass-by-value semantics, we need to create an auxiliary pointer to
      // the actual argument and use it instead of the actual argument. This
      // is because `byval` arguments are pointers, so each reference to those
      // arguments assume they are dealing with pointers.
      auto &temp{provenance.temp_decls[arg]};
      if (!temp) {
        auto addr_of_arg{ast.CreateAddrOf(ref)};
        auto func{arg->getParent()};
        auto fdecl{provenance.value_decls[func]->getAsFunction()};
        auto argdecl{
            clang::cast<clang::ParmVarDecl>(provenance.value_decls[arg])};
        temp = ast.CreateVarDecl(fdecl, GetQualType(arg->getType()),
                                 argdecl->getName().str() + "_ptr");
        temp->setInit(addr_of_arg);
        fdecl->addDecl(temp);
      }

      res = ast.CreateDeclRef(temp);
    } else {
      res = ref;
    }
  } else if (auto inst = llvm::dyn_cast<llvm::Instruction>(val)) {
    // Operand is a result of an expression
    if (auto decl = provenance.value_decls[inst]) {
      res = ast.CreateDeclRef(decl);
    } else {
      res = visit(inst);
    }
  } else {
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
  }
  provenance.use_provenance[res] = &val;
  return res;
}

clang::Expr *ExprGen::visitMemCpyInst(llvm::MemCpyInst &inst) {
  DLOG(INFO) << "visitMemCpyInst: " << LLVMThingToString(&inst);

  std::vector<clang::Expr *> args;
  for (auto i{0U}; i < 3; ++i) {
    auto &arg{inst.getArgOperandUse(i)};
    args.push_back(CreateOperandExpr(arg));
  }

  return ast.CreateBuiltinCall(clang::Builtin::BI__builtin_memcpy, args);
}

clang::Expr *ExprGen::visitMemCpyInlineInst(llvm::MemCpyInlineInst &inst) {
  DLOG(INFO) << "visitMemCpyInlineInst: " << LLVMThingToString(&inst);

  std::vector<clang::Expr *> args;
  for (auto i{0U}; i < 3; ++i) {
    auto &arg{inst.getArgOperandUse(i)};
    args.push_back(CreateOperandExpr(arg));
  }

  return ast.CreateBuiltinCall(clang::Builtin::BI__builtin_memcpy, args);
}

clang::Expr *ExprGen::visitAnyMemMoveInst(llvm::AnyMemMoveInst &inst) {
  DLOG(INFO) << "visitAnyMemMoveInst: " << LLVMThingToString(&inst);

  std::vector<clang::Expr *> args;
  for (auto i{0U}; i < 3; ++i) {
    auto &arg{inst.getArgOperandUse(i)};
    args.push_back(CreateOperandExpr(arg));
  }

  return ast.CreateBuiltinCall(clang::Builtin::BI__builtin_memmove, args);
}

clang::Expr *ExprGen::visitAnyMemSetInst(llvm::AnyMemSetInst &inst) {
  DLOG(INFO) << "visitAnyMemSetInst: " << LLVMThingToString(&inst);

  std::vector<clang::Expr *> args;
  for (auto i{0U}; i < 3; ++i) {
    auto &arg{inst.getArgOperandUse(i)};
    args.push_back(CreateOperandExpr(arg));
  }

  return ast.CreateBuiltinCall(clang::Builtin::BI__builtin_memset, args);
}

clang::Expr *ExprGen::visitIntrinsicInst(llvm::IntrinsicInst &inst) {
  DLOG(INFO) << "visitIntrinsicInst: " << LLVMThingToString(&inst);

  // NOTE(artem): As of this writing, rellic does not do anything
  // with debug intrinsics and debug metadata. Processing them to C
  // is useless (since there is no C equivalent anyway).
  // All it does it lead to triggering of asserts for things that are
  // low-priority on the "to fix" list

  if (llvm::isDbgInfoIntrinsic(inst.getIntrinsicID())) {
    DLOG(INFO) << "Skipping debug data intrinsic";
    return nullptr;
  }

  if (IsAnnotationIntrinsic(inst.getIntrinsicID())) {
    // Some of this overlaps with the debug data case above.
    // This is fine. We want debug data special cased as we know it is present
    // and we may make use of it earlier than other annotations
    DLOG(INFO) << "Skipping non-debug annotation";
    return nullptr;
  }

  // handle this as a CallInst, which IntrinsicInst derives from
  return visitCallInst(inst);
}

clang::Expr *ExprGen::visitCallInst(llvm::CallInst &inst) {
  DLOG(INFO) << "visitCallInst: " << LLVMThingToString(&inst);

  std::vector<clang::Expr *> args;
  for (auto i{0U}; i < inst.getNumArgOperands(); ++i) {
    auto &arg{inst.getArgOperandUse(i)};
    auto opnd{CreateOperandExpr(arg)};
    if (inst.getParamAttr(i, llvm::Attribute::ByVal).isValid()) {
      opnd = ast.CreateDeref(opnd);
      provenance.use_provenance[opnd] = &arg;
    }
    args.push_back(opnd);
  }

  clang::Expr *callexpr{nullptr};
  auto &callee{*(inst.op_end() - 1)};
  if (auto func = llvm::dyn_cast<llvm::Function>(callee)) {
    auto fdecl{provenance.value_decls[func]->getAsFunction()};
    callexpr = ast.CreateCall(fdecl, args);
  } else if (auto iasm = llvm::dyn_cast<llvm::InlineAsm>(callee)) {
    auto fdecl{provenance.value_decls[iasm]->getAsFunction()};
    callexpr = ast.CreateCall(fdecl, args);
  } else if (llvm::isa<llvm::PointerType>(callee->getType())) {
    callexpr = ast.CreateCall(CreateOperandExpr(callee), args);
  } else {
    LOG(FATAL) << "Callee is not a function";
  }

  return callexpr;
}

clang::Expr *ExprGen::visitGetElementPtrInst(llvm::GetElementPtrInst &inst) {
  DLOG(INFO) << "visitGetElementPtrInst: " << LLVMThingToString(&inst);

  auto indexed_type{inst.getPointerOperandType()};
  auto base{
      CreateOperandExpr(inst.getOperandUse(inst.getPointerOperandIndex()))};

  for (auto &idx : llvm::make_range(inst.idx_begin(), inst.idx_end())) {
    switch (indexed_type->getTypeID()) {
      // Initial pointer
      case llvm::Type::PointerTyID: {
        CHECK(idx == *inst.idx_begin())
            << "Indexing an llvm::PointerType is only valid at first index";
        base = ast.CreateArraySub(base, CreateOperandExpr(idx));
        indexed_type =
            llvm::cast<llvm::PointerType>(indexed_type)->getElementType();
      } break;
      // Arrays
      case llvm::Type::ArrayTyID: {
        base = ast.CreateArraySub(base, CreateOperandExpr(idx));
        indexed_type =
            llvm::cast<llvm::ArrayType>(indexed_type)->getElementType();
      } break;
      // Structures
      case llvm::Type::StructTyID: {
        auto mem_idx = llvm::dyn_cast<llvm::ConstantInt>(idx);
        CHECK(mem_idx) << "Non-constant GEP index while indexing a structure";
        auto tdecl{provenance.type_decls[indexed_type]};
        CHECK(tdecl) << "Structure declaration doesn't exist";
        auto record{clang::cast<clang::RecordDecl>(tdecl)};
        auto field_it{record->field_begin()};
        std::advance(field_it, mem_idx->getLimitedValue());
        CHECK(field_it != record->field_end()) << "GEP index is out of bounds";
        base = ast.CreateDot(base, *field_it);
        indexed_type =
            llvm::cast<llvm::StructType>(indexed_type)->getTypeAtIndex(idx);
      } break;

      default: {
        // Vectors
        if (indexed_type->isVectorTy()) {
          auto l_vec_ty{llvm::cast<llvm::VectorType>(indexed_type)};
          auto l_elm_ty{l_vec_ty->getElementType()};
          auto c_elm_ty{GetQualType(l_elm_ty)};
          base = ast.CreateCStyleCast(ast_ctx.getPointerType(c_elm_ty),
                                      ast.CreateAddrOf(base));
          base = ast.CreateArraySub(base, CreateOperandExpr(idx));
          indexed_type = l_elm_ty;
        } else {
          THROW() << "Indexing an unknown type: "
                  << LLVMThingToString(indexed_type);
        }
      } break;
    }
  }

  return ast.CreateAddrOf(base);
}

clang::Expr *ExprGen::visitInstruction(llvm::Instruction &inst) {
  THROW() << "Instruction not supported: " << LLVMThingToString(&inst);
  return nullptr;
}

clang::Expr *ExprGen::visitExtractValueInst(llvm::ExtractValueInst &inst) {
  DLOG(INFO) << "visitExtractValueInst: " << LLVMThingToString(&inst);

  auto base{CreateOperandExpr(inst.getOperandUse(0))};
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
        auto tdecl{provenance.type_decls[indexed_type]};
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
        THROW() << "Indexing an unknown aggregate type";
        break;
    }
  }

  return base;
}

clang::Expr *ExprGen::visitLoadInst(llvm::LoadInst &inst) {
  DLOG(INFO) << "visitLoadInst: " << LLVMThingToString(&inst);
  return ast.CreateDeref(CreateOperandExpr(inst.getOperandUse(0)));
}

clang::Expr *ExprGen::visitBinaryOperator(llvm::BinaryOperator &inst) {
  DLOG(INFO) << "visitBinaryOperator: " << LLVMThingToString(&inst);
  // Get operands
  auto lhs{CreateOperandExpr(inst.getOperandUse(0))};
  auto rhs{CreateOperandExpr(inst.getOperandUse(1))};
  // Sign-cast int operand
  auto IntSignCast{[this](clang::Expr *operand, bool sign) {
    auto type{ast_ctx.getIntTypeForBitwidth(
        ast_ctx.getTypeSize(operand->getType()), sign)};
    return ast.CreateCStyleCast(type, operand);
  }};
  clang::Expr *res;
  // Where the magic happens
  switch (inst.getOpcode()) {
    case llvm::BinaryOperator::LShr:
      res = ast.CreateShr(IntSignCast(lhs, false), rhs);
      break;

    case llvm::BinaryOperator::AShr:
      res = ast.CreateShr(IntSignCast(lhs, true), rhs);
      break;

    case llvm::BinaryOperator::Shl:
      res = ast.CreateShl(lhs, rhs);
      break;

    case llvm::BinaryOperator::And:
      res = inst.getType()->isIntegerTy(1U) ? ast.CreateLAnd(lhs, rhs)
                                            : ast.CreateAnd(lhs, rhs);
      break;

    case llvm::BinaryOperator::Or:
      res = inst.getType()->isIntegerTy(1U) ? ast.CreateLOr(lhs, rhs)
                                            : ast.CreateOr(lhs, rhs);
      break;

    case llvm::BinaryOperator::Xor:
      res = ast.CreateXor(lhs, rhs);
      break;

    case llvm::BinaryOperator::URem:
      res = ast.CreateRem(IntSignCast(lhs, false), IntSignCast(rhs, false));
      break;

    case llvm::BinaryOperator::SRem:
      res = ast.CreateRem(IntSignCast(lhs, true), IntSignCast(rhs, true));
      break;

    case llvm::BinaryOperator::UDiv:
      res = ast.CreateDiv(IntSignCast(lhs, false), IntSignCast(rhs, false));
      break;

    case llvm::BinaryOperator::SDiv:
      res = ast.CreateDiv(IntSignCast(lhs, true), IntSignCast(rhs, true));
      break;

    case llvm::BinaryOperator::FDiv:
      res = ast.CreateDiv(lhs, rhs);
      break;

    case llvm::BinaryOperator::Add:
    case llvm::BinaryOperator::FAdd:
      res = ast.CreateAdd(lhs, rhs);
      break;

    case llvm::BinaryOperator::Sub:
    case llvm::BinaryOperator::FSub:
      res = ast.CreateSub(lhs, rhs);
      break;

    case llvm::BinaryOperator::Mul:
    case llvm::BinaryOperator::FMul:
      res = ast.CreateMul(lhs, rhs);
      break;

    default:
      THROW() << "Unknown BinaryOperator: " << inst.getOpcodeName();
      return nullptr;
  }
  return res;
}

clang::Expr *ExprGen::visitCmpInst(llvm::CmpInst &inst) {
  DLOG(INFO) << "visitCmpInst: " << LLVMThingToString(&inst);
  // Get operands
  auto lhs{CreateOperandExpr(inst.getOperandUse(0))};
  auto rhs{CreateOperandExpr(inst.getOperandUse(1))};
  // Sign-cast int operand
  auto IntSignCast{[this](clang::Expr *op, bool sign) {
    auto ot{op->getType()};
    auto rt{ast_ctx.getIntTypeForBitwidth(ast_ctx.getTypeSize(ot), sign)};
    if (rt == ot) {
      return op;
    } else {
      auto cast{ast.CreateCStyleCast(rt, op)};
      CopyProvenance(op, cast, provenance.use_provenance);
      return (clang::Expr *)cast;
    }
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
  clang::Expr *res;
  std::vector<clang::Expr *> args{lhs, rhs};
  // Where the magic happens
  switch (inst.getPredicate()) {
    case llvm::CmpInst::ICMP_UGT:
    case llvm::CmpInst::ICMP_SGT:
    case llvm::CmpInst::FCMP_OGT:
      res = ast.CreateGT(lhs, rhs);
      break;

    case llvm::CmpInst::ICMP_ULT:
    case llvm::CmpInst::ICMP_SLT:
    case llvm::CmpInst::FCMP_OLT:
      res = ast.CreateLT(lhs, rhs);
      break;

    case llvm::CmpInst::ICMP_UGE:
    case llvm::CmpInst::ICMP_SGE:
    case llvm::CmpInst::FCMP_OGE:
      res = ast.CreateGE(lhs, rhs);
      break;

    case llvm::CmpInst::ICMP_ULE:
    case llvm::CmpInst::ICMP_SLE:
    case llvm::CmpInst::FCMP_OLE:
      res = ast.CreateLE(lhs, rhs);
      break;

    case llvm::CmpInst::ICMP_EQ:
    case llvm::CmpInst::FCMP_OEQ:
      res = ast.CreateEQ(lhs, rhs);
      break;

    case llvm::CmpInst::ICMP_NE:
      res = ast.CreateNE(lhs, rhs);
      break;

    case llvm::CmpInst::FCMP_UGT:
      res = ast.CreateBuiltinCall(clang::Builtin::BI__builtin_isgreater, args);
      break;

    case llvm::CmpInst::FCMP_ULT:
      res = ast.CreateBuiltinCall(clang::Builtin::BI__builtin_isless, args);
      break;

    case llvm::CmpInst::FCMP_UGE:
      res = ast.CreateBuiltinCall(clang::Builtin::BI__builtin_isgreaterequal,
                                  args);
      break;

    case llvm::CmpInst::FCMP_ULE:
      res =
          ast.CreateBuiltinCall(clang::Builtin::BI__builtin_islessequal, args);
      break;

    case llvm::CmpInst::FCMP_UNE:
      res = ast.CreateBuiltinCall(clang::Builtin::BI__builtin_islessgreater,
                                  args);
      break;

    case llvm::CmpInst::FCMP_UNO:
      res =
          ast.CreateBuiltinCall(clang::Builtin::BI__builtin_isunordered, args);
      break;

    case llvm::CmpInst::FCMP_TRUE:
      res = ast.CreateTrue();
      break;

    case llvm::CmpInst::FCMP_FALSE:
      res = ast.CreateFalse();
      break;

    default:
      THROW() << "Unknown CmpInst predicate: " << inst.getOpcodeName();
      return nullptr;
  }
  return res;
}

clang::Expr *ExprGen::visitCastInst(llvm::CastInst &inst) {
  DLOG(INFO) << "visitCastInst: " << LLVMThingToString(&inst);
  // There should always be an operand with a cast instruction
  // Get a C-language expression of the operand
  auto operand{CreateOperandExpr(inst.getOperandUse(0))};
  // Get destination type
  auto type{GetQualType(inst.getType())};
  // Adjust type
  switch (inst.getOpcode()) {
    case llvm::CastInst::Trunc: {
      auto bitwidth{ast_ctx.getTypeSize(type)};
      auto sign{operand->getType()->isSignedIntegerType()};
      type = ast_ctx.getIntTypeForBitwidth(bitwidth, sign);
    } break;

    case llvm::CastInst::ZExt: {
      auto bitwidth{ast_ctx.getTypeSize(type)};
      type = ast_ctx.getIntTypeForBitwidth(bitwidth, /*signed=*/0U);
    } break;

    case llvm::CastInst::SExt: {
      auto bitwidth{ast_ctx.getTypeSize(type)};
      type = ast_ctx.getIntTypeForBitwidth(bitwidth, /*signed=*/1U);
    } break;

    case llvm::CastInst::AddrSpaceCast:
      // NOTE(artem): ignore addrspace casts for now, but eventually we want
      // to handle them as __thread or some other context-relative handler
      // which will *highly* depend on the target architecture
      DLOG(WARNING)
          << __FUNCTION__
          << ": Ignoring an AddrSpaceCast because it is not yet implemented.";
      // The fallthrough is intentional here
      [[clang::fallthrough]];

    case llvm::CastInst::BitCast:
    case llvm::CastInst::PtrToInt:
    case llvm::CastInst::IntToPtr:
    case llvm::CastInst::SIToFP:
    case llvm::CastInst::FPToUI:
    case llvm::CastInst::FPToSI:
    case llvm::CastInst::FPExt:
    case llvm::CastInst::FPTrunc:
      break;

    default: {
      THROW() << "Unknown CastInst cast type";
    } break;
  }
  // Create cast
  return ast.CreateCStyleCast(type, operand);
}

clang::Expr *ExprGen::visitSelectInst(llvm::SelectInst &inst) {
  DLOG(INFO) << "visitSelectInst: " << LLVMThingToString(&inst);

  auto cond{CreateOperandExpr(inst.getOperandUse(0))};
  auto tval{CreateOperandExpr(inst.getOperandUse(1))};
  auto fval{CreateOperandExpr(inst.getOperandUse(2))};

  return ast.CreateConditional(cond, tval, fval);
}

clang::Expr *ExprGen::visitFreezeInst(llvm::FreezeInst &inst) {
  DLOG(INFO) << "visitFreezeInst: " << LLVMThingToString(&inst);

  return CreateOperandExpr(inst.getOperandUse(0));
}

clang::Expr *ExprGen::visitUnaryOperator(llvm::UnaryOperator &inst) {
  DLOG(INFO) << "visitUnaryOperator: " << LLVMThingToString(&inst);

  THROW_IF(inst.getOpcode() != llvm::UnaryOperator::FNeg)
      << "Unsupported UnaryOperator: " << LLVMThingToString(&inst);

  auto opnd{CreateOperandExpr(inst.getOperandUse(0))};
  return ast.CreateUnaryOp(clang::UO_Minus, opnd);
}

// StmtGen is tasked with populating blocks with their top-level
// statements. It delegates to IRToASTVisitor for generating the expressions
// used by each statement.
//
// Most instructions in a LLVM BasicBlock are actually free of side effects and
// only make sense as part of expressions. The handful of instructions that
// actually have potential side effects are handled here, by creating
// top-level statements for them, and eventually even storing their result in a
// local variable if it was deemed necessary by
// IRToASTVisitor::VisitFunctionDecl
class StmtGen : public llvm::InstVisitor<StmtGen, clang::Stmt *> {
 private:
  clang::ASTContext &ast_ctx;
  ASTBuilder &ast;
  ExprGen &expr_gen;
  Provenance &provenance;

  void GetOrCreateIntrinsic(llvm::InlineAsm *val) {
    auto &decl{provenance.value_decls[val]};
    if (decl) {
      return;
    }

    auto tudecl{ast_ctx.getTranslationUnitDecl()};
    auto name{"asm_" +
              std::to_string(GetNumDecls<clang::FunctionDecl>(tudecl))};
    auto type{expr_gen.GetQualType(val->getType()->getPointerElementType())};
    decl = ast.CreateFunctionDecl(tudecl, type, name);
  }

 public:
  StmtGen(clang::ASTContext &ast_ctx, ASTBuilder &ast, ExprGen &expr_gen,
          Provenance &provenance)
      : ast_ctx(ast_ctx),
        ast(ast),
        expr_gen(expr_gen),
        provenance(provenance) {}

  clang::Stmt *visitStoreInst(llvm::StoreInst &inst);
  clang::Stmt *visitCallInst(llvm::CallInst &inst);
  clang::Stmt *visitReturnInst(llvm::ReturnInst &inst);
  clang::Stmt *visitAllocaInst(llvm::AllocaInst &inst);
  clang::Stmt *visitBranchInst(llvm::BranchInst &inst);
  clang::Stmt *visitSwitchInst(llvm::SwitchInst &inst);
  clang::Stmt *visitUnreachableInst(llvm::UnreachableInst &inst);
  clang::Stmt *visitPHINode(llvm::PHINode &inst);
  clang::Stmt *visitInstruction(llvm::Instruction &inst);
};

clang::Stmt *StmtGen::visitStoreInst(llvm::StoreInst &inst) {
  DLOG(INFO) << "visitStoreInst: " << LLVMThingToString(&inst);
  // Stores in LLVM IR correspond to value assignments in C
  // Get the operand we're assigning to
  auto lhs{expr_gen.CreateOperandExpr(
      inst.getOperandUse(inst.getPointerOperandIndex()))};
  // Get the operand we're assigning from
  auto &value_opnd{inst.getOperandUse(0)};
  if (auto undef = llvm::dyn_cast<llvm::UndefValue>(value_opnd)) {
    DLOG(INFO) << "Invalid store ignored: " << LLVMThingToString(&inst);
    return nullptr;
  }
  auto rhs{expr_gen.CreateOperandExpr(value_opnd)};
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

clang::Stmt *StmtGen::visitCallInst(llvm::CallInst &inst) {
  auto &var{provenance.value_decls[&inst]};
  auto expr{expr_gen.visit(inst)};
  if (var) {
    return ast.CreateAssign(ast.CreateDeclRef(var), expr);
  }
  return expr;
}

clang::Stmt *StmtGen::visitReturnInst(llvm::ReturnInst &inst) {
  DLOG(INFO) << "visitReturnInst: " << LLVMThingToString(&inst);
  if (auto retval = inst.getReturnValue()) {
    return ast.CreateReturn(expr_gen.CreateOperandExpr(inst.getOperandUse(0)));
  } else {
    return ast.CreateReturn();
  }
}

clang::Stmt *StmtGen::visitAllocaInst(llvm::AllocaInst &inst) {
  // Variable declarations for allocas have already been generated by
  // `VisitFunctionDecl`
  return nullptr;
}

clang::Stmt *StmtGen::visitBranchInst(llvm::BranchInst &inst) {
  DLOG(INFO) << "visitBranchInst ignored: " << LLVMThingToString(&inst);
  return nullptr;
}

clang::Stmt *StmtGen::visitSwitchInst(llvm::SwitchInst &inst) {
  DLOG(INFO) << "visitSwitchInst ignored: " << LLVMThingToString(&inst);
  return nullptr;
}

clang::Stmt *StmtGen::visitUnreachableInst(llvm::UnreachableInst &inst) {
  DLOG(INFO) << "visitUnreachableInst ignored:" << LLVMThingToString(&inst);
  return nullptr;
}

clang::Stmt *StmtGen::visitPHINode(llvm::PHINode &inst) { return nullptr; }

clang::Stmt *StmtGen::visitInstruction(llvm::Instruction &inst) {
  auto &var{provenance.value_decls[&inst]};
  if (var) {
    auto expr{expr_gen.visit(inst)};
    return ast.CreateAssign(ast.CreateDeclRef(var), expr);
  }
  return nullptr;
}

IRToASTVisitor::IRToASTVisitor(clang::ASTUnit &unit, Provenance &provenance)
    : ast_unit(unit),
      ast_ctx(unit.getASTContext()),
      ast(unit),
      provenance(provenance) {}

void IRToASTVisitor::VisitGlobalVar(llvm::GlobalVariable &gvar) {
  ExprGen expr_gen{ast_unit, provenance};
  expr_gen.VisitGlobalVar(gvar);
}

void IRToASTVisitor::VisitArgument(llvm::Argument &arg) {
  DLOG(INFO) << "VisitArgument: " << LLVMThingToString(&arg);
  auto &parm{provenance.value_decls[&arg]};
  if (parm) {
    return;
  }
  // Create a name
  auto name{arg.hasName() ? arg.getName().str()
                          : "arg" + std::to_string(arg.getArgNo())};
  // Get parent function declaration
  auto func{arg.getParent()};
  auto fdecl{clang::cast<clang::FunctionDecl>(provenance.value_decls[func])};
  auto argtype{arg.getType()};
  if (arg.hasByValAttr()) {
    auto byval{arg.getAttribute(llvm::Attribute::ByVal)};
    argtype = byval.getValueAsType();
  }
  ExprGen expr_gen{ast_unit, provenance};
  // Create a declaration
  parm = ast.CreateParamDecl(fdecl, expr_gen.GetQualType(argtype), name);
}

// This function fixes function types for those functions that have arguments
// that are passed by value using the `byval` attribute.
// They need special treatment because those arguments, instead of actually
// being passed by value, are instead passed "by reference" from a bitcode point
// of view, with the caveat that the actual semantics are more like "create a
// copy of the reference before calling, and pass a pointer to that copy
// instead" (this is done implicitly).
// Thus, we need to convert a function type like
//   i32 @do_foo(%struct.foo* byval(%struct.foo) align 4 %f)
// into
//   i32 @do_foo(%struct.foo %f)
static llvm::FunctionType *GetFixedFunctionType(llvm::Function &func) {
  std::vector<llvm::Type *> new_arg_types{};

  for (auto &arg : func.args()) {
    if (arg.hasByValAttr()) {
      auto ptrtype{llvm::cast<llvm::PointerType>(arg.getType())};
      new_arg_types.push_back(ptrtype->getElementType());
    } else {
      new_arg_types.push_back(arg.getType());
    }
  }

  return llvm::FunctionType::get(func.getReturnType(), new_arg_types,
                                 func.isVarArg());
}

void IRToASTVisitor::VisitBasicBlock(llvm::BasicBlock &block,
                                     std::vector<clang::Stmt *> &stmts) {
  ExprGen expr_gen{ast_unit, provenance};
  StmtGen stmt_gen{ast_ctx, ast, expr_gen, provenance};
  for (auto &inst : block) {
    auto stmt{stmt_gen.visit(inst)};
    if (stmt) {
      stmts.push_back(stmt);
      provenance.stmt_provenance[stmt] = &inst;
    }
  }

  auto uses{provenance.outgoing_uses.equal_range(&block)};
  for (auto it{uses.first}; it != uses.second; ++it) {
    auto use{it->second};
    auto var{provenance.value_decls[use->getUser()]};
    auto expr{expr_gen.CreateOperandExpr(*use)};
    stmts.push_back(ast.CreateAssign(ast.CreateDeclRef(var), expr));
  }
}

void IRToASTVisitor::VisitFunctionDecl(llvm::Function &func) {
  auto name{func.getName().str()};
  DLOG(INFO) << "VisitFunctionDecl: " << name;

  if (IsAnnotationIntrinsic(func.getIntrinsicID())) {
    DLOG(INFO) << "Skipping creating declaration for LLVM intrinsic";
    return;
  }

  auto &decl{provenance.value_decls[&func]};
  if (decl) {
    return;
  }

  DLOG(INFO) << "Creating FunctionDecl for " << name;
  auto tudecl{ast_ctx.getTranslationUnitDecl()};
  ExprGen expr_gen{ast_unit, provenance};
  auto type{expr_gen.GetQualType(GetFixedFunctionType(func))};
  decl = ast.CreateFunctionDecl(tudecl, type, name);

  tudecl->addDecl(decl);

  std::vector<clang::ParmVarDecl *> params;
  for (auto &arg : func.args()) {
    VisitArgument(arg);
    params.push_back(
        clang::cast<clang::ParmVarDecl>(provenance.value_decls[&arg]));
  }

  auto fdecl{decl->getAsFunction()};
  fdecl->setParams(params);

  for (auto &inst : llvm::instructions(func)) {
    auto &var{provenance.value_decls[&inst]};
    if (auto alloca = llvm::dyn_cast<llvm::AllocaInst>(&inst)) {
      auto name{"var" + std::to_string(GetNumDecls<clang::VarDecl>(fdecl))};
      // TLDR: Here we discard the variable name as present in the bitcode
      // because there probably is a better one we can assign afterwards, using
      // debug metadata.
      //
      // The rationale behind discarding the bitcode-provided name here is that
      // such a name is generally only present if the code was compiled with
      // debug info enabled, in which case more specific info is available in
      // later passes. If debug metadata is not present, we don't have variable
      // names either.
      // Also, the "autogenerated" name is used in the renaming passes in order
      // to disambiguate between variables which were originally in different
      // scopes but have the same name: if, for example, we had two variables
      // named `a`, we disambiguate by renaming one `a_var1`. If we instead use
      // the name as provided by the bitcode, we would end up with something
      // like `a_a_addr`
      // (`varname_addr` being a common name used by clang for variables used as
      // storage for parameters e.g. a parameter named "foo" has a corresponding
      // local variable named "foo_addr").
      var = ast.CreateVarDecl(
          fdecl, expr_gen.GetQualType(alloca->getAllocatedType()), name);
      fdecl->addDecl(var);
    } else if (inst.hasNUsesOrMore(2) || llvm::isa<llvm::CallInst>(inst) ||
               llvm::isa<llvm::LoadInst>(inst) ||
               llvm::isa<llvm::PHINode>(inst)) {
      if (!inst.getType()->isVoidTy()) {
        auto name{"val" + std::to_string(GetNumDecls<clang::VarDecl>(fdecl))};
        auto type{expr_gen.GetQualType(inst.getType())};
        if (auto arrayType = clang::dyn_cast<clang::ArrayType>(type)) {
          type = ast_ctx.getPointerType(arrayType->getElementType());
        }

        var = ast.CreateVarDecl(fdecl, type, name);
        fdecl->addDecl(var);

        if (auto phi = llvm::dyn_cast<llvm::PHINode>(&inst)) {
          for (auto i{0U}; i < phi->getNumIncomingValues(); ++i) {
            auto bb{phi->getIncomingBlock(i)};
            auto &use{phi->getOperandUse(i)};

            provenance.outgoing_uses.insert({bb, &use});
          }
        }
      }
    }
  }
}

clang::Expr *IRToASTVisitor::CreateOperandExpr(llvm::Use &val) {
  ExprGen expr_gen{ast_unit, provenance};
  return expr_gen.CreateOperandExpr(val);
}

clang::Expr *IRToASTVisitor::CreateConstantExpr(llvm::Constant *constant) {
  ExprGen expr_gen{ast_unit, provenance};
  return expr_gen.CreateConstantExpr(constant);
}

}  // namespace rellic
