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

IRToASTVisitor::IRToASTVisitor(clang::ASTUnit &unit, Provenance &provenance)
    : ast_ctx(unit.getASTContext()), ast(unit), provenance(provenance) {}

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

clang::Expr *IRToASTVisitor::CreateConstantExpr(llvm::Constant *constant) {
  if (auto cexpr = llvm::dyn_cast<llvm::ConstantExpr>(constant)) {
    auto inst{cexpr->getAsInstruction()};
    auto expr{visit(inst)};
    provenance.use_provenance.erase(expr);
    DeleteValue(inst);
    return expr;
  } else if (auto global = llvm::dyn_cast<llvm::GlobalValue>(constant)) {
    auto decl{GetOrCreateDecl(global)};
    auto ref{ast.CreateDeclRef(clang::cast<clang::ValueDecl>(decl))};
    return ast.CreateAddrOf(ref);
  }
  return CreateLiteralExpr(constant);
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

clang::Expr *IRToASTVisitor::GetOperandExpr(llvm::Use &val) {
  DLOG(INFO) << "Getting Expr for " << LLVMThingToString(val);
  auto CreateRef{[this, &val] {
    auto decl{GetOrCreateDecl(val)};
    auto ref{ast.CreateDeclRef(clang::cast<clang::ValueDecl>(decl))};
    provenance.use_provenance[ref] = &val;
    return ref;
  }};

  auto Wrapper{[&]() -> clang::Expr * {
    // Operand is a constant value
    if (auto constant = llvm::dyn_cast<llvm::Constant>(val)) {
      return CreateConstantExpr(constant);
    }
    // Operand is an l-value (variable, function, ...)
    if (llvm::isa<llvm::AllocaInst>(val)) {
      // Add a `&` operator
      return ast.CreateAddrOf(CreateRef());
    }
    // Operand is a function argument or local variable
    if (llvm::isa<llvm::Argument>(val)) {
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
          auto fdecl{GetOrCreateDecl(func)->getAsFunction()};
          auto argdecl{
              clang::cast<clang::ParmVarDecl>(provenance.value_decls[arg])};
          temp = ast.CreateVarDecl(fdecl, GetQualType(arg->getType()),
                                   argdecl->getName().str() + "_ptr");
          temp->setInit(addr_of_arg);
          fdecl->addDecl(temp);
        }

        return ast.CreateDeclRef(temp);
      } else {
        return ref;
      }
    }
    // Operand is a result of an expression
    if (auto inst = llvm::dyn_cast<llvm::Instruction>(val)) {
      if (auto decl = provenance.value_decls[inst]) {
        return ast.CreateDeclRef(decl);
      } else {
        return visit(inst);
      }
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
  }};
  auto res{Wrapper()};
  provenance.use_provenance[res] = &val;
  return res;
}

clang::Decl *IRToASTVisitor::GetOrCreateIntrinsic(llvm::InlineAsm *val) {
  auto &decl{provenance.value_decls[val]};
  if (decl) {
    return decl;
  }

  auto tudecl{ast_ctx.getTranslationUnitDecl()};
  auto name{"asm_" + std::to_string(GetNumDecls<clang::FunctionDecl>(tudecl))};
  auto type{GetQualType(val->getType()->getPointerElementType())};
  decl = ast.CreateFunctionDecl(tudecl, type, name);

  return decl;
}

clang::Decl *IRToASTVisitor::GetOrCreateDecl(llvm::Value *val) {
  auto &decl{provenance.value_decls[val]};
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
    THROW() << "Unsupported value type: " << LLVMThingToString(val);
  }

  return decl;
}

void IRToASTVisitor::VisitGlobalVar(llvm::GlobalVariable &gvar) {
  DLOG(INFO) << "VisitGlobalVar: " << LLVMThingToString(&gvar);
  auto &var{provenance.value_decls[&gvar]};
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
        CreateConstantExpr(gvar.getInitializer()));
  }
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
  auto fdecl{clang::cast<clang::FunctionDecl>(GetOrCreateDecl(func))};
  auto argtype{arg.getType()};
  if (arg.hasByValAttr()) {
    auto byval{arg.getAttribute(llvm::Attribute::ByVal)};
    argtype = byval.getValueAsType();
  }
  // Create a declaration
  parm = ast.CreateParamDecl(fdecl, GetQualType(argtype), name);
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
  auto type{GetQualType(GetFixedFunctionType(func))};
  decl = ast.CreateFunctionDecl(tudecl, type, name);

  tudecl->addDecl(decl);

  std::vector<clang::ParmVarDecl *> params;
  for (auto &arg : func.args()) {
    auto parm{clang::cast<clang::ParmVarDecl>(GetOrCreateDecl(&arg))};
    params.push_back(parm);
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
      var = ast.CreateVarDecl(fdecl, GetQualType(alloca->getAllocatedType()),
                              name);
      fdecl->addDecl(var);
    } else if (inst.hasNUsesOrMore(2) || llvm::isa<llvm::CallInst>(inst) ||
               llvm::isa<llvm::LoadInst>(inst)) {
      if (!inst.getType()->isVoidTy()) {
        auto name{"val" + std::to_string(GetNumDecls<clang::VarDecl>(fdecl))};
        auto type{GetQualType(inst.getType())};
        if (auto arrayType = clang::dyn_cast<clang::ArrayType>(type)) {
          type = ast_ctx.getPointerType(arrayType->getElementType());
        }

        var = ast.CreateVarDecl(fdecl, type, name);
        fdecl->addDecl(var);
      }
    }
  }
}

clang::Expr *IRToASTVisitor::visitMemCpyInst(llvm::MemCpyInst &inst) {
  DLOG(INFO) << "visitMemCpyInst: " << LLVMThingToString(&inst);

  std::vector<clang::Expr *> args;
  for (auto i{0U}; i < 3; ++i) {
    auto &arg{inst.getArgOperandUse(i)};
    args.push_back(GetOperandExpr(arg));
  }

  return ast.CreateBuiltinCall(clang::Builtin::BI__builtin_memcpy, args);
}

clang::Expr *IRToASTVisitor::visitMemCpyInlineInst(
    llvm::MemCpyInlineInst &inst) {
  DLOG(INFO) << "visitMemCpyInlineInst: " << LLVMThingToString(&inst);

  std::vector<clang::Expr *> args;
  for (auto i{0U}; i < 3; ++i) {
    auto &arg{inst.getArgOperandUse(i)};
    args.push_back(GetOperandExpr(arg));
  }

  return ast.CreateBuiltinCall(clang::Builtin::BI__builtin_memcpy, args);
}

clang::Expr *IRToASTVisitor::visitAnyMemMoveInst(llvm::AnyMemMoveInst &inst) {
  DLOG(INFO) << "visitAnyMemMoveInst: " << LLVMThingToString(&inst);

  std::vector<clang::Expr *> args;
  for (auto i{0U}; i < 3; ++i) {
    auto &arg{inst.getArgOperandUse(i)};
    args.push_back(GetOperandExpr(arg));
  }

  return ast.CreateBuiltinCall(clang::Builtin::BI__builtin_memmove, args);
}

clang::Expr *IRToASTVisitor::visitAnyMemSetInst(llvm::AnyMemSetInst &inst) {
  DLOG(INFO) << "visitAnyMemSetInst: " << LLVMThingToString(&inst);

  std::vector<clang::Expr *> args;
  for (auto i{0U}; i < 3; ++i) {
    auto &arg{inst.getArgOperandUse(i)};
    args.push_back(GetOperandExpr(arg));
  }

  return ast.CreateBuiltinCall(clang::Builtin::BI__builtin_memset, args);
}

clang::Expr *IRToASTVisitor::visitIntrinsicInst(llvm::IntrinsicInst &inst) {
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

clang::Expr *IRToASTVisitor::visitCallInst(llvm::CallInst &inst) {
  DLOG(INFO) << "visitCallInst: " << LLVMThingToString(&inst);

  std::vector<clang::Expr *> args;
  for (auto i{0U}; i < inst.getNumArgOperands(); ++i) {
    auto &arg{inst.getArgOperandUse(i)};
    auto opnd{GetOperandExpr(arg)};
    if (inst.getParamAttr(i, llvm::Attribute::ByVal).isValid()) {
      opnd = ast.CreateDeref(opnd);
      provenance.use_provenance[opnd] = &arg;
    }
    args.push_back(opnd);
  }

  clang::Expr *callexpr{nullptr};
  auto &callee{*(inst.op_end() - 1)};
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

  return callexpr;
}

clang::Expr *IRToASTVisitor::visitGetElementPtrInst(
    llvm::GetElementPtrInst &inst) {
  DLOG(INFO) << "visitGetElementPtrInst: " << LLVMThingToString(&inst);

  auto indexed_type{inst.getPointerOperandType()};
  auto base{GetOperandExpr(inst.getOperandUse(inst.getPointerOperandIndex()))};

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
          base = ast.CreateArraySub(base, GetOperandExpr(idx));
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

clang::Expr *IRToASTVisitor::visitInstruction(llvm::Instruction &inst) {
  THROW() << "Instruction not supported: " << LLVMThingToString(&inst);
  return nullptr;
}

clang::Expr *IRToASTVisitor::visitExtractValueInst(
    llvm::ExtractValueInst &inst) {
  DLOG(INFO) << "visitExtractValueInst: " << LLVMThingToString(&inst);

  auto base{GetOperandExpr(inst.getOperandUse(0))};
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

clang::Expr *IRToASTVisitor::visitLoadInst(llvm::LoadInst &inst) {
  DLOG(INFO) << "visitLoadInst: " << LLVMThingToString(&inst);
  return ast.CreateDeref(GetOperandExpr(inst.getOperandUse(0)));
}

clang::Expr *IRToASTVisitor::visitBinaryOperator(llvm::BinaryOperator &inst) {
  DLOG(INFO) << "visitBinaryOperator: " << LLVMThingToString(&inst);
  // Get operands
  auto lhs{GetOperandExpr(inst.getOperandUse(0))};
  auto rhs{GetOperandExpr(inst.getOperandUse(1))};
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

clang::Expr *IRToASTVisitor::visitCmpInst(llvm::CmpInst &inst) {
  DLOG(INFO) << "visitCmpInst: " << LLVMThingToString(&inst);
  // Get operands
  auto lhs{GetOperandExpr(inst.getOperandUse(0))};
  auto rhs{GetOperandExpr(inst.getOperandUse(1))};
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
    case llvm::CmpInst::FCMP_UNE:
      res = ast.CreateNE(lhs, rhs);
      break;

    default:
      THROW() << "Unknown CmpInst predicate: " << inst.getOpcodeName();
      return nullptr;
  }
  return res;
}

clang::Expr *IRToASTVisitor::visitCastInst(llvm::CastInst &inst) {
  DLOG(INFO) << "visitCastInst: " << LLVMThingToString(&inst);
  // There should always be an operand with a cast instruction
  // Get a C-language expression of the operand
  auto operand{GetOperandExpr(inst.getOperandUse(0))};
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
      // NOTE(artem): ignore addrspace casts for now, but eventually we want to
      // handle them as __thread or some other context-relative handler
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

clang::Expr *IRToASTVisitor::visitSelectInst(llvm::SelectInst &inst) {
  DLOG(INFO) << "visitSelectInst: " << LLVMThingToString(&inst);

  auto cond{GetOperandExpr(inst.getOperandUse(0))};
  auto tval{GetOperandExpr(inst.getOperandUse(1))};
  auto fval{GetOperandExpr(inst.getOperandUse(2))};

  return ast.CreateConditional(cond, tval, fval);
}

clang::Expr *IRToASTVisitor::visitFreezeInst(llvm::FreezeInst &inst) {
  DLOG(INFO) << "visitFreezeInst: " << LLVMThingToString(&inst);

  return GetOperandExpr(inst.getOperandUse(0));
}

clang::Expr *IRToASTVisitor::visitPHINode(llvm::PHINode &inst) {
  DLOG(INFO) << "visitPHINode: " << LLVMThingToString(&inst);
  THROW() << "Unexpected llvm::PHINode. Try running llvm's reg2mem pass "
             "before decompiling.";
  return nullptr;
}

}  // namespace rellic
