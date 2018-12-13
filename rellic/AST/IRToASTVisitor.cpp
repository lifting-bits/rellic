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

#include <clang/AST/Expr.h>

#include "remill/BC/Util.h"

#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/Util.h"

namespace rellic {

namespace {

static clang::QualType GetQualType(clang::ASTContext &ctx, llvm::Type *type,
                                   bool constant = false) {
  // DLOG(INFO) << "GetQualType: " << (constant ? "constant " : "")
  //            << remill::LLVMThingToString(type);
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
      auto ret = GetQualType(ctx, func->getReturnType(), constant);
      std::vector<clang::QualType> params;
      for (auto param : func->params()) {
        params.push_back(GetQualType(ctx, param, constant));
      }
      auto epi = clang::FunctionProtoType::ExtProtoInfo();
      epi.Variadic = func->isVarArg();
      result = ctx.getFunctionType(ret, params, epi);
    } break;

    case llvm::Type::PointerTyID: {
      auto ptr = llvm::cast<llvm::PointerType>(type);
      result =
          ctx.getPointerType(GetQualType(ctx, ptr->getElementType(), constant));
    } break;

    case llvm::Type::ArrayTyID: {
      auto arr = llvm::cast<llvm::ArrayType>(type);
      auto elm = GetQualType(ctx, arr->getElementType(), constant);
      if (constant) {
        result = ctx.getConstantArrayType(
            elm, llvm::APInt(32, arr->getNumElements()),
            clang::ArrayType::ArraySizeModifier::Normal, 0);
      } else {
        LOG(FATAL) << "Unknown LLVM ArrayType";
      }
    } break;

    default:
      LOG(FATAL) << "Unknown LLVM Type";
      break;
  }

  return result;
}

static clang::Expr *CreateLiteralExpr(clang::ASTContext &ast_ctx,
                                      clang::DeclContext *decl_ctx,
                                      llvm::Constant *constant) {
  DLOG(INFO) << "Creating literal Expr for "
             << remill::LLVMThingToString(constant);

  auto type = GetQualType(ast_ctx, constant->getType(), /*constant=*/true);

  clang::Expr *result = nullptr;
  if (auto integer = llvm::dyn_cast<llvm::ConstantInt>(constant)) {
    result = clang::IntegerLiteral::Create(ast_ctx, integer->getValue(), type,
                                           clang::SourceLocation());
  } else if (auto floating = llvm::dyn_cast<llvm::ConstantFP>(constant)) {
    result = clang::FloatingLiteral::Create(ast_ctx, floating->getValueAPF(),
                                            /*isexact=*/true, type,
                                            clang::SourceLocation());
  } else if (auto array = llvm::dyn_cast<llvm::ConstantDataArray>(constant)) {
    CHECK(array->isString()) << "ConstantArray is not a string";
    result = clang::StringLiteral::Create(
        ast_ctx, array->getAsString(), clang::StringLiteral::StringKind::Ascii,
        /*Pascal=*/false, type, clang::SourceLocation());
  }

  return result;
}

static clang::VarDecl *CreateVarDecl(clang::ASTContext &ast_ctx,
                                     clang::DeclContext *decl_ctx,
                                     llvm::Type *type, std::string name,
                                     bool constant = false) {
  DLOG(INFO) << "Creating VarDecl for " << name;
  return clang::VarDecl::Create(
      ast_ctx, decl_ctx, clang::SourceLocation(), clang::SourceLocation(),
      CreateIdentifier(ast_ctx, name), GetQualType(ast_ctx, type, constant),
      nullptr, clang::SC_None);
}

}  // namespace

IRToASTVisitor::IRToASTVisitor(clang::CompilerInstance &ins)
    : cc_ins(&ins), ast_ctx(cc_ins->getASTContext()) {}

clang::FunctionDecl *IRToASTVisitor::GetFunctionDecl(llvm::Instruction *inst) {
  return llvm::dyn_cast<clang::FunctionDecl>(
      decls[inst->getParent()->getParent()]);
}

clang::Expr *IRToASTVisitor::GetOperandExpr(clang::DeclContext *decl_ctx,
                                            llvm::Value *val) {
  DLOG(INFO) << "Getting Expr for " << remill::LLVMThingToString(val);
  clang::Expr *result = nullptr;

  if (auto cexpr = llvm::dyn_cast<llvm::ConstantExpr>(val)) {
    auto inst = cexpr->getAsInstruction();
    visit(inst);
    stmts[val] = stmts[inst];
    stmts.erase(inst);
  }

  if (auto cdata = llvm::dyn_cast<llvm::ConstantData>(val)) {
    // Operand is a constant
    result = CreateLiteralExpr(ast_ctx, decl_ctx, cdata);
  } else if (decls.count(val)) {
    // Operand is an l-value (variable, function, ...)
    auto decl = llvm::cast<clang::ValueDecl>(decls[val]);
    result = CreateDeclRefExpr(ast_ctx, decl);
    if (llvm::isa<llvm::GlobalValue>(val)) {
      // LLVM IR global values are constant pointers; Add a `&`
      result = new (ast_ctx) clang::UnaryOperator(
          result, clang::UO_AddrOf, GetQualType(ast_ctx, val->getType()),
          clang::VK_RValue, clang::OK_Ordinary, clang::SourceLocation());
    }
  } else if (stmts.count(val)) {
    // Operand is a result of an expression
    result = llvm::cast<clang::Expr>(stmts[val]);
  } else {
    LOG(FATAL) << "Invalid operand";
  }

  if (!result) {
    LOG(WARNING) << "Operand expression for the given value does not exist";
  }

  return result;
}

clang::Stmt *IRToASTVisitor::GetOrCreateStmt(llvm::Value *val) {
  auto &stmt = stmts[val];
  if (!stmt) {
    if (auto inst = llvm::dyn_cast<llvm::Instruction>(val)) {
      visit(inst->getParent());
    } else {
      LOG(FATAL) << "Unsupported value type";
    }
  }
  return stmt;
}

clang::Decl *IRToASTVisitor::GetOrCreateDecl(llvm::Value *val) {
  auto &decl = decls[val];
  if (!decl) {
    if (auto func = llvm::dyn_cast<llvm::Function>(val)) {
      VisitFunctionDecl(*func);
    } else if (auto gvar = llvm::dyn_cast<llvm::GlobalVariable>(val)) {
      VisitGlobalVar(*gvar);
    } else if (auto inst = llvm::dyn_cast<llvm::AllocaInst>(val)) {
      visitAllocaInst(*inst);
    } else {
      LOG(FATAL) << "Unsupported value type";
    }
  }
  return decl;
}

void IRToASTVisitor::VisitGlobalVar(llvm::GlobalVariable &gvar) {
  DLOG(INFO) << "VisitGlobalVar: " << remill::LLVMThingToString(&gvar);
  auto &var = decls[&gvar];
  if (!var) {
    auto name = gvar.getName().str();
    auto tudecl = ast_ctx.getTranslationUnitDecl();
    auto type = llvm::cast<llvm::PointerType>(gvar.getType())->getElementType();

    var = CreateVarDecl(ast_ctx, tudecl, type, name, /*constant=*/true);

    if (gvar.hasInitializer()) {
      auto tmp = llvm::cast<clang::VarDecl>(var);
      tmp->setInit(CreateLiteralExpr(ast_ctx, tudecl, gvar.getInitializer()));
    }

    tudecl->addDecl(var);
  }
}

void IRToASTVisitor::VisitFunctionDecl(llvm::Function &func) {
  auto name = func.getName().str();
  DLOG(INFO) << "VisitFunctionDecl: " << name;
  auto &decl = decls[&func];
  if (!decl) {
    DLOG(INFO) << "Creating FunctionDecl for " << name;
    auto tudecl = ast_ctx.getTranslationUnitDecl();
    auto type = llvm::cast<llvm::PointerType>(func.getType())->getElementType();

    decl = clang::FunctionDecl::Create(
        ast_ctx, tudecl, clang::SourceLocation(), clang::SourceLocation(),
        clang::DeclarationName(CreateIdentifier(ast_ctx, name)),
        GetQualType(ast_ctx, type), nullptr, clang::SC_None, false);

    if (!func.arg_empty()) {
      auto func_ctx = llvm::cast<clang::FunctionDecl>(decl);
      std::vector<clang::ParmVarDecl *> params;
      for (auto &arg : func.args()) {
        auto arg_name = arg.hasName() ? arg.getName().str()
                                      : "arg" + std::to_string(arg.getArgNo());

        DLOG(INFO) << "Creating ParmVarDecl for " << arg_name;

        auto param = clang::ParmVarDecl::Create(
            ast_ctx, func_ctx, clang::SourceLocation(), clang::SourceLocation(),
            CreateIdentifier(ast_ctx, arg_name),
            GetQualType(ast_ctx, arg.getType()), nullptr, clang::SC_None,
            nullptr);

        decls[&arg] = param;
        params.push_back(param);
      }

      func_ctx->setParams(params);
    }

    tudecl->addDecl(decl);
  }
}

// void IRToASTVisitor::VisitFunctionDefn(llvm::Function &func) {
//   auto name = func.getName().str();
//   DLOG(INFO) << "VisitFunctionDefn: " << name;
//   auto &compound = stmts[&func];
//   if (!compound) {
//     std::vector<clang::Stmt *> compounds;
//     for (auto &block : func) {
//       auto stmt = stmts[&block];
//       CHECK(stmt) << "CompoundStmt for block does not exist";
//       compounds.push_back(stmt);
//     }

//     compound = new (ast_ctx) clang::CompoundStmt(
//         ast_ctx, compounds, clang::SourceLocation(),
//         clang::SourceLocation());

//     if (auto decl = llvm::dyn_cast<clang::FunctionDecl>(decls[&func])) {
//       decl->setBody(compound);
//     } else {
//       LOG(FATAL) << "FunctionDecl for function does not exist";
//     }
//   }
// }

// void IRToASTVisitor::VisitBasicBlock(llvm::BasicBlock &block) {
//   auto name = block.hasName() ? block.getName().str() : "<no_name>";
//   DLOG(INFO) << "VisitBasicBlock: " << name;
//   auto &compound = stmts[&block];
//   if (!compound) {
//     DLOG(INFO) << "Creating CompoundStmt for " << name;
//     visit(block);
//     std::vector<clang::Stmt *> block_stmts;
//     for (auto &inst : block) {
//       if (stmts.count(&inst)) {
//         block_stmts.push_back(stmts[&inst]);
//       }
//     }
//     compound = new (ast_ctx) clang::CompoundStmt(
//         ast_ctx, block_stmts, clang::SourceLocation(),
//         clang::SourceLocation());
//   }
// }

void IRToASTVisitor::visitCallInst(llvm::CallInst &inst) {
  DLOG(INFO) << "visitCallInst: " << remill::LLVMThingToString(&inst);
  auto &callexpr = stmts[&inst];
  if (!callexpr) {
    auto callee = inst.getCalledValue();
    if (auto func = llvm::dyn_cast<llvm::Function>(callee)) {
      auto decl = decls[func];
      CHECK(decl) << "FunctionDecl for callee does not exist";
      auto fdecl = llvm::cast<clang::FunctionDecl>(decl);
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

      callexpr = new (ast_ctx)
          clang::CallExpr(ast_ctx, fcast, args, fdecl->getReturnType(),
                          clang::VK_RValue, clang::SourceLocation());
    } else {
      LOG(FATAL) << "Callee is not a function";
    }
  }
}

void IRToASTVisitor::visitGetElementPtrInst(llvm::GetElementPtrInst &inst) {
  DLOG(INFO) << "visitGetElementPtrInst: " << remill::LLVMThingToString(&inst);
  auto &expr = stmts[&inst];
  if (!expr) {
    auto src_type = inst.getSourceElementType();
    if (llvm::isa<llvm::ArrayType>(src_type)) {
      DLOG(INFO) << "Indexing an array";
      if (inst.hasAllZeroIndices()) {
        auto ptr = inst.getPointerOperand();
        if (auto array = llvm::dyn_cast<clang::VarDecl>(decls[ptr])) {
          expr = CreateDeclRefExpr(ast_ctx, array);
        } else {
          LOG(FATAL) << "Referencing undeclared variable";
        }
      }
    } else if (llvm::isa<llvm::StructType>(src_type)) {
      DLOG(INFO) << "Indexing a structure";
    }
  }
}

void IRToASTVisitor::visitAllocaInst(llvm::AllocaInst &inst) {
  DLOG(INFO) << "visitAllocaInst: " << remill::LLVMThingToString(&inst);
  auto &declstmt = stmts[&inst];
  if (!declstmt) {
    auto &var = decls[&inst];
    if (!var) {
      auto fdecl = GetFunctionDecl(&inst);

      CHECK(fdecl) << "Undeclared function";

      auto name = inst.hasName()
                      ? inst.getName().str()
                      : "var" + std::to_string(std::distance(
                                    fdecl->decls_begin(), fdecl->decls_end()));

      var = CreateVarDecl(ast_ctx, fdecl, inst.getAllocatedType(), name);
      fdecl->addDecl(var);
    }

    declstmt = new (ast_ctx)
        clang::DeclStmt(clang::DeclGroupRef(var), clang::SourceLocation(),
                        clang::SourceLocation());
  }
}

void IRToASTVisitor::visitStoreInst(llvm::StoreInst &inst) {
  DLOG(INFO) << "visitStoreInst: " << remill::LLVMThingToString(&inst);
  auto &assign = stmts[&inst];
  if (!assign) {
    // Stores in LLVM IR correspond to value assignments in C
    auto fdecl = GetFunctionDecl(&inst);
    CHECK(fdecl) << "Undeclared function";
    // Get the operand we're assigning to
    auto ptr = inst.getPointerOperand();
    clang::Expr *lhs = GetOperandExpr(fdecl, ptr);
    // Strip `&` from the global variable reference
    if (llvm::isa<llvm::GlobalVariable>(ptr)) {
      if (auto unary = llvm::dyn_cast<clang::UnaryOperator>(lhs)) {
        if (unary->getOpcode() == clang::UO_AddrOf) {
          lhs = unary->getSubExpr();
        }
      }
    }
    CHECK(lhs) << "Invalid assigned to operand";
    // Get the operand we're assigning from
    auto val = inst.getValueOperand();
    clang::Expr *rhs = GetOperandExpr(fdecl, val);
    CHECK(rhs) << "Invalid assigned from operand";
    // Create the assignemnt itself
    assign = new (ast_ctx) clang::BinaryOperator(
        lhs, rhs, clang::BO_Assign,
        GetQualType(
            ast_ctx,
            llvm::cast<llvm::PointerType>(ptr->getType())->getElementType()),
        clang::VK_RValue, clang::OK_Ordinary, clang::SourceLocation(),
        /*fpContractable=*/false);
  }
}

void IRToASTVisitor::visitLoadInst(llvm::LoadInst &inst) {
  DLOG(INFO) << "visitLoadInst: " << remill::LLVMThingToString(&inst);
  auto &ref = stmts[&inst];
  if (!ref) {
    auto fdecl = GetFunctionDecl(&inst);
    CHECK(fdecl) << "Undeclared function";
    auto ptr = inst.getPointerOperand();
    if (llvm::isa<llvm::AllocaInst>(ptr) ||
        llvm::isa<llvm::GlobalVariable>(ptr)) {
      DLOG(INFO) << "Loading from a variable";
      if (auto var = llvm::dyn_cast<clang::VarDecl>(GetOrCreateDecl(ptr))) {
        ref = CreateDeclRefExpr(ast_ctx, var);
      } else {
        LOG(FATAL) << "Referencing undeclared variable";
      }
    } else if (llvm::isa<llvm::GetElementPtrInst>(ptr)) {
      DLOG(INFO) << "Loading from an aggregate";
    } else {
      LOG(FATAL) << "Loading from an unknown pointer";
    }
  }
}

void IRToASTVisitor::visitReturnInst(llvm::ReturnInst &inst) {
  DLOG(INFO) << "visitReturnInst: " << remill::LLVMThingToString(&inst);
  auto &retstmt = stmts[&inst];
  if (!retstmt) {
    if (auto retval = inst.getReturnValue()) {
      auto fdecl = GetFunctionDecl(&inst);
      auto retexpr = GetOperandExpr(fdecl, retval);
      retstmt = new (ast_ctx)
          clang::ReturnStmt(clang::SourceLocation(), retexpr, nullptr);
    } else {
      retstmt = new (ast_ctx) clang::ReturnStmt(clang::SourceLocation());
    }
  }
}

void IRToASTVisitor::visitBinaryOperator(llvm::BinaryOperator &inst) {
  DLOG(INFO) << "visitBinaryOperator: " << remill::LLVMThingToString(&inst);
  auto &binop = stmts[&inst];
  if (!binop) {
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
}

void IRToASTVisitor::visitCmpInst(llvm::CmpInst &inst) {
  DLOG(INFO) << "visitCmpInst: " << remill::LLVMThingToString(&inst);
  auto &cmp = stmts[&inst];
  if (!cmp) {
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
}

// void IRToASTVisitor::visitInstruction(llvm::Instruction &inst) {
//   DLOG(INFO) << "visitInstruction: " << remill::LLVMThingToString(&inst);
// }

}  // namespace rellic