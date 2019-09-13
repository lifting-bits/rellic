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

#include "rellic/AST/Util.h"
#include "rellic/AST/Z3ConvVisitor.h"

namespace rellic {

namespace {

static unsigned GetZ3SortSize(z3::sort sort) {
  switch (sort.sort_kind()) {
    case Z3_BOOL_SORT:
      return 1;
      break;

    case Z3_BV_SORT:
      return sort.bv_size();
      break;

    case Z3_FLOATING_POINT_SORT: {
      auto &ctx = sort.ctx();
      return Z3_fpa_get_sbits(ctx, sort) + Z3_fpa_get_ebits(ctx, sort);
    } break;

    case Z3_UNINTERPRETED_SORT:
      return 0;
      break;

    default:
      LOG(FATAL) << "Unknown Z3 sort: " << sort;
      break;
  }
}

static std::string CreateZ3DeclName(clang::NamedDecl *decl) {
  std::stringstream ss;
  ss << std::hex << decl << std::dec;
  ss << '_' << decl->getNameAsString();
  return ss.str();
}

}  // namespace

Z3ConvVisitor::Z3ConvVisitor(clang::ASTContext *c_ctx, z3::context *z_ctx)
    : ast_ctx(c_ctx),
      z3_ctx(z_ctx),
      z3_expr_vec(*z3_ctx),
      z3_decl_vec(*z3_ctx) {}

// Inserts a `clang::Expr` <=> `z3::expr` mapping into
void Z3ConvVisitor::InsertZ3Expr(clang::Expr *c_expr, z3::expr z3_expr) {
  auto iter = z3_expr_map.find(c_expr);
  CHECK(iter == z3_expr_map.end());
  z3_expr_map[c_expr] = z3_expr_vec.size();
  z3_expr_vec.push_back(z3_expr);
}

// Retrieves a `z3::expr` corresponding to `c_expr`.
// The `z3::expr` needs to be created and inserted by
// `Z3ConvVisistor::InsertZ3Expr` first.
z3::expr Z3ConvVisitor::GetZ3Expr(clang::Expr *c_expr) {
  auto iter = z3_expr_map.find(c_expr);
  CHECK(iter != z3_expr_map.end());
  return z3_expr_vec[iter->second];
}

// Inserts a `clang::ValueDecl` <=> `z3::func_decl` mapping into
void Z3ConvVisitor::InsertZ3Decl(clang::ValueDecl *c_decl,
                                 z3::func_decl z3_decl) {
  auto iter = z3_decl_map.find(c_decl);
  CHECK(iter == z3_decl_map.end());
  z3_decl_map[c_decl] = z3_decl_vec.size();
  z3_decl_vec.push_back(z3_decl);
}

// Retrieves a `z3::func_decl` corresponding to `c_decl`.
// The `z3::func_decl` needs to be created and inserted by
// `Z3ConvVisistor::InsertZ3Decl` first.
z3::func_decl Z3ConvVisitor::GetZ3Decl(clang::ValueDecl *c_decl) {
  auto iter = z3_decl_map.find(c_decl);
  CHECK(iter != z3_decl_map.end());
  return z3_decl_vec[iter->second];
}

// If `expr` is not boolean, returns a `z3::expr` that corresponds
// to the non-boolean to boolean expression cast in C. Otherwise
// returns `expr`.
z3::expr Z3ConvVisitor::Z3BoolCast(z3::expr expr) {
  if (expr.is_bool()) {
    return expr;
  } else {
    auto cast = expr != z3_ctx->num_val(0, expr.get_sort());
    return cast.simplify();
  }
}

void Z3ConvVisitor::InsertCExpr(z3::expr z3_expr, clang::Expr *c_expr) {
  auto hash = z3_expr.hash();
  auto iter = c_expr_map.find(hash);
  CHECK(iter == c_expr_map.end());
  c_expr_map[hash] = c_expr;
}

clang::Expr *Z3ConvVisitor::GetCExpr(z3::expr z3_expr) {
  auto hash = z3_expr.hash();
  auto iter = c_expr_map.find(hash);
  CHECK(iter != c_expr_map.end());
  return c_expr_map[hash];
}

void Z3ConvVisitor::InsertCValDecl(z3::func_decl z3_decl,
                                   clang::ValueDecl *c_decl) {
  auto id = Z3_get_func_decl_id(*z3_ctx, z3_decl);
  auto iter = c_decl_map.find(id);
  CHECK(iter == c_decl_map.end());
  c_decl_map[id] = c_decl;
}

clang::ValueDecl *Z3ConvVisitor::GetCValDecl(z3::func_decl z3_decl) {
  auto id = Z3_get_func_decl_id(*z3_ctx, z3_decl);
  auto iter = c_decl_map.find(id);
  CHECK(iter != c_decl_map.end());
  return c_decl_map[id];
}

z3::sort Z3ConvVisitor::GetZ3Sort(clang::QualType type) {
  // Booleans
  if (type->isBooleanType()) {
    return z3_ctx->bool_sort();
  }
  // Structures
  if (type->isStructureType()) {
    auto decl = clang::cast<clang::RecordType>(type)->getDecl();
    auto name = decl->getNameAsString().c_str();
    auto sort = z3_ctx->uninterpreted_sort(name);
    auto id = Z3_get_sort_id(*z3_ctx, sort);
    c_type_decl_map[id] = decl;
    return sort;
  }
  auto bitwidth = ast_ctx->getTypeSize(type);
  // Floating points
  if (type->isRealFloatingType()) {
    switch (bitwidth) {
      case 16:
        // return z3_ctx.fpa_sort<16>();
        return z3::to_sort(*z3_ctx, Z3_mk_fpa_sort_16(*z3_ctx));
        break;

      case 32:
        return z3::to_sort(*z3_ctx, Z3_mk_fpa_sort_32(*z3_ctx));
        break;

      case 64:
        return z3::to_sort(*z3_ctx, Z3_mk_fpa_sort_64(*z3_ctx));
        break;

      case 128:
        return z3::to_sort(*z3_ctx, Z3_mk_fpa_sort_128(*z3_ctx));
        break;

      default:
        LOG(FATAL) << "Unsupported floating-point bitwidth!";
        break;
    }
  }
  // Default to bitvectors
  return z3::to_sort(*z3_ctx, Z3_mk_bv_sort(*z3_ctx, bitwidth));
}

clang::QualType Z3ConvVisitor::GetQualType(z3::sort z3_sort) {
  // Get sort size
  auto size = GetZ3SortSize(z3_sort);
  // Determine C type for Z3 sort
  clang::QualType result;
  switch (z3_sort.sort_kind()) {
    // Bools
    case Z3_BOOL_SORT:
      result = ast_ctx->BoolTy;
      break;
    // Bitvectors
    case Z3_BV_SORT:
      result = ast_ctx->getIntTypeForBitwidth(size, 0);
      break;
    // IEEE 754 floating-points
    case Z3_FLOATING_POINT_SORT:
      result = ast_ctx->getRealTypeForBitwidth(size);
      break;
    // Uninterpreted
    case Z3_UNINTERPRETED_SORT: {
      auto id = Z3_get_sort_id(*z3_ctx, z3_sort);
      auto iter = c_type_decl_map.find(id);
      CHECK(iter != c_type_decl_map.end())
          << "Unknown Z3 uninterpreted sort: " << z3_sort;
      result = ast_ctx->getTypeDeclType(iter->second);
    } break;

    // Unknowns
    default:
      LOG(FATAL) << "Unknown Z3 sort: " << z3_sort;
      break;
  }

  CHECK(!result.isNull()) << "Unknown C type for " << z3_sort;

  return result;
}

clang::Expr *Z3ConvVisitor::CreateLiteralExpr(z3::expr z3_expr) {
  DLOG(INFO) << "Creating literal clang::Expr for " << z3_expr;

  auto sort = z3_expr.get_sort();
  auto type = GetQualType(sort);
  auto size = ast_ctx->getTypeSize(type);

  clang::Expr *result = nullptr;

  switch (sort.sort_kind()) {
    case Z3_BOOL_SORT: {
      type = ast_ctx->UnsignedIntTy;
      size = ast_ctx->getIntWidth(type);
      llvm::APInt val(size, z3_expr.bool_value() == Z3_L_TRUE ? 1 : 0);
      result = CreateIntegerLiteral(*ast_ctx, val, type);
    } break;

    case Z3_BV_SORT: {
      llvm::APInt val(size, Z3_get_numeral_string(z3_expr.ctx(), z3_expr), 10);
      result = CreateIntegerLiteral(*ast_ctx, val, type);
    } break;

    case Z3_FLOATING_POINT_SORT: {
      const llvm::fltSemantics *semantics;
      switch (size) {
        case 16:
          semantics = &llvm::APFloat::IEEEhalf();
          break;
        case 32:
          semantics = &llvm::APFloat::IEEEsingle();
          break;
        case 64:
          semantics = &llvm::APFloat::IEEEdouble();
          break;
        case 128:
          semantics = &llvm::APFloat::IEEEquad();
          break;
        default:
          LOG(FATAL) << "Unknown Z3 floating-point sort!";
          break;
      }
      llvm::APInt ival(size, Z3_get_numeral_string(z3_expr.ctx(), z3_expr), 10);
      llvm::APFloat fval(*semantics, ival);
      result = CreateFloatingLiteral(*ast_ctx, fval, type);
    } break;

    default:
      LOG(FATAL) << "Unknown Z3 sort: " << sort;
      break;
  }

  return result;
}

// Retrieves or creates`z3::expr`s from `clang::Expr`.
z3::expr Z3ConvVisitor::GetOrCreateZ3Expr(clang::Expr *c_expr) {
  if (!z3_expr_map.count(c_expr)) {
    TraverseStmt(c_expr);
  }
  return GetZ3Expr(c_expr);
}

z3::func_decl Z3ConvVisitor::GetOrCreateZ3Decl(clang::ValueDecl *c_decl) {
  if (!z3_decl_map.count(c_decl)) {
    TraverseDecl(c_decl);
  }

  auto z3_decl = GetZ3Decl(c_decl);

  auto id = Z3_get_func_decl_id(*z3_ctx, z3_decl);
  if (!c_decl_map.count(id)) {
    InsertCValDecl(z3_decl, c_decl);
  }

  return z3_decl;
}

// Retrieves or creates `clang::Expr` from `z3::expr`.
clang::Expr *Z3ConvVisitor::GetOrCreateCExpr(z3::expr z3_expr) {
  if (!c_expr_map.count(z3_expr.hash())) {
    VisitZ3Expr(z3_expr);
  }
  return GetCExpr(z3_expr);
}

bool Z3ConvVisitor::VisitVarDecl(clang::VarDecl *var) {
  auto name = var->getNameAsString().c_str();
  DLOG(INFO) << "VisitVarDecl: " << name;
  if (z3_decl_map.count(var)) {
    DLOG(INFO) << "Re-declaration of " << name << "; Returning.";
    return true;
  }

  auto z3_name = CreateZ3DeclName(var);
  auto z3_sort = GetZ3Sort(var->getType());
  auto z3_const = z3_ctx->constant(z3_name.c_str(), z3_sort);

  InsertZ3Decl(var, z3_const.decl());

  return true;
}

bool Z3ConvVisitor::VisitFieldDecl(clang::FieldDecl *field) {
  auto name = field->getNameAsString().c_str();
  DLOG(INFO) << "VisitFieldDecl: " << name;
  if (z3_decl_map.count(field)) {
    DLOG(INFO) << "Re-declaration of " << name << "; Returning.";
    return true;
  }

  auto z3_name = CreateZ3DeclName(field->getParent()) + "_" + name;
  auto z3_sort = GetZ3Sort(field->getType());
  auto z3_const = z3_ctx->constant(z3_name.c_str(), z3_sort);

  InsertZ3Decl(field, z3_const.decl());

  return true;
}

bool Z3ConvVisitor::VisitFunctionDecl(clang::FunctionDecl *func) {
  DLOG(INFO) << "VisitFunctionDecl";
  LOG(FATAL) << "Unimplemented FunctionDecl visitor";
  return true;
}

bool Z3ConvVisitor::VisitCStyleCastExpr(clang::CStyleCastExpr *cast) {
  DLOG(INFO) << "VisitCStyleCastExpr";
  if (z3_expr_map.count(cast)) {
    return true;
  }

  auto z3_subexpr = GetOrCreateZ3Expr(cast->getSubExpr());

  switch (cast->getCastKind()) {
    case clang::CastKind::CK_PointerToIntegral:
    case clang::CastKind::CK_IntegralToPointer:
    case clang::CastKind::CK_IntegralCast:
    case clang::CastKind::CK_NullToPointer: {
      CHECK(z3_subexpr.is_bv())
          << "Integral or pointer cast operand is not a bit-vector!";
      auto src = cast->getSubExpr()->getType();
      auto dst = cast->getType();
      long diff = ast_ctx->getTypeSize(dst) - ast_ctx->getTypeSize(src);
      // in case nothing happens
      auto expr = z3_subexpr;
      // bitwise extensions
      if (diff > 0) {
        expr = src->isSignedIntegerType() ? z3::sext(z3_subexpr, diff)
                                          : z3::zext(z3_subexpr, diff);
      // truncs
      } else if (diff < 0) {
        expr = z3_subexpr.extract(std::abs(diff), 1);
      }
      InsertZ3Expr(cast, expr);
    } break;

    default:
      LOG(FATAL) << "Unsupported cast type: " << cast->getCastKindName();
      break;
  }

  return true;
}

bool Z3ConvVisitor::VisitImplicitCastExpr(clang::ImplicitCastExpr *cast) {
  DLOG(INFO) << "VisitImplicitCastExpr";
  if (z3_expr_map.count(cast)) {
    return true;
  }

  auto z3_subexpr = GetOrCreateZ3Expr(cast->getSubExpr());

  switch (cast->getCastKind()) {
    case clang::CastKind::CK_IntegralCast: {
      auto src = cast->getSubExpr()->getType();
      auto dst = cast->getType();
      auto diff = ast_ctx->getTypeSize(dst) - ast_ctx->getTypeSize(src);
      CHECK(diff > 0) << "Negative bit extension";
      auto z3_cast = src->isSignedIntegerType() ? z3::sext(z3_subexpr, diff)
                                                : z3::zext(z3_subexpr, diff);
      InsertZ3Expr(cast, z3_cast);
    } break;

    case clang::CastKind::CK_ArrayToPointerDecay: {
      CHECK(z3_subexpr.is_bv() && z3_subexpr.is_const())
          << "Pointer cast operand is not a bit-vector constant";
      auto ptr_sort = GetZ3Sort(cast->getType());
      auto arr_sort = z3_subexpr.get_sort();
      auto z3_ptr_decay = z3_ctx->function("PtrDecay", arr_sort, ptr_sort);
      InsertZ3Expr(cast, z3_ptr_decay(z3_subexpr));
    } break;

    default:
      LOG(FATAL) << "Unsupported cast type: " << cast->getCastKindName();
      break;
  }

  return true;
}

bool Z3ConvVisitor::VisitArraySubscriptExpr(clang::ArraySubscriptExpr *sub) {
  DLOG(INFO) << "VisitArraySubscriptExpr";
  if (z3_expr_map.count(sub)) {
    return true;
  }
  // Get base
  auto z3_base = GetOrCreateZ3Expr(sub->getBase());
  auto base_sort = z3_base.get_sort();
  CHECK(base_sort.is_bv()) << "Invalid Z3 sort for base expression";
  // Get index
  auto z3_idx = GetOrCreateZ3Expr(sub->getIdx());
  auto idx_sort = z3_idx.get_sort();
  CHECK(idx_sort.is_bv()) << "Invalid Z3 sort for index expression";
  // Get result
  auto elm_sort = GetZ3Sort(sub->getType());
  // Create a z3_function
  auto z3_arr_sub = z3_ctx->function("ArraySub", base_sort, idx_sort, elm_sort);
  // Create a z3 expression
  InsertZ3Expr(sub, z3_arr_sub(z3_base, z3_idx));
  // Done
  return true;
}

bool Z3ConvVisitor::VisitMemberExpr(clang::MemberExpr *expr) {
  DLOG(INFO) << "VisitMemberExpr";
  if (z3_expr_map.count(expr)) {
    return true;
  }

  auto z3_mem = GetOrCreateZ3Decl(expr->getMemberDecl())();
  auto z3_base = GetOrCreateZ3Expr(expr->getBase());
  auto z3_mem_expr = z3_ctx->function("Member", z3_base.get_sort(),
                                      z3_mem.get_sort(), z3_mem.get_sort());

  InsertZ3Expr(expr, z3_mem_expr(z3_base, z3_mem));

  return true;
}

bool Z3ConvVisitor::VisitCallExpr(clang::CallExpr *call) {
  LOG(FATAL) << "Unimplemented CallExpr visitor";
  return true;
}

// Translates clang unary operators expressions to Z3 equivalents.
bool Z3ConvVisitor::VisitParenExpr(clang::ParenExpr *parens) {
  DLOG(INFO) << "VisitParenExpr";
  if (z3_expr_map.count(parens)) {
    return true;
  }

  auto z3_subexpr = GetOrCreateZ3Expr(parens->getSubExpr());

  switch (z3_subexpr.decl().decl_kind()) {
    // Parens may affect semantics of C expressions
    case Z3_OP_UNINTERPRETED: {
      auto sort = z3_subexpr.get_sort();
      auto z3_paren = z3_ctx->function("Paren", sort, sort);
      InsertZ3Expr(parens, z3_paren(z3_subexpr));
    } break;
    // Default to ignoring the parens, Z3 should know how
    // to interpret them.
    default:
      InsertZ3Expr(parens, z3_subexpr);
      break;
  }

  return true;
}

// Translates clang unary operators expressions to Z3 equivalents.
bool Z3ConvVisitor::VisitUnaryOperator(clang::UnaryOperator *c_op) {
  DLOG(INFO) << "VisitUnaryOperator: "
             << c_op->getOpcodeStr(c_op->getOpcode()).str();
  if (z3_expr_map.count(c_op)) {
    return true;
  }
  // Get operand
  auto operand = GetOrCreateZ3Expr(c_op->getSubExpr());
  // Create z3 unary op
  switch (c_op->getOpcode()) {
    case clang::UO_LNot: {
      InsertZ3Expr(c_op, !Z3BoolCast(operand));
    } break;

    case clang::UO_AddrOf: {
      auto ptr_sort = GetZ3Sort(c_op->getType());
      auto z3_addrof = z3_ctx->function("AddrOf", operand.get_sort(), ptr_sort);
      InsertZ3Expr(c_op, z3_addrof(operand));
    } break;

    case clang::UO_Deref: {
      auto elm_sort = GetZ3Sort(c_op->getType());
      auto z3_deref = z3_ctx->function("Deref", operand.get_sort(), elm_sort);
      InsertZ3Expr(c_op, z3_deref(operand));
    } break;

    default:
      LOG(FATAL) << "Unknown clang::UnaryOperator operation!";
      break;
  }
  return true;
}

// Translates clang binary operators expressions to Z3 equivalents.
bool Z3ConvVisitor::VisitBinaryOperator(clang::BinaryOperator *c_op) {
  DLOG(INFO) << "VisitBinaryOperator: " << c_op->getOpcodeStr().str();
  if (z3_expr_map.count(c_op)) {
    return true;
  }
  // Get operands
  auto lhs = GetOrCreateZ3Expr(c_op->getLHS());
  auto rhs = GetOrCreateZ3Expr(c_op->getRHS());
  // Create z3 binary op
  switch (c_op->getOpcode()) {
    case clang::BO_LAnd:
      InsertZ3Expr(c_op, Z3BoolCast(lhs) && Z3BoolCast(rhs));
      break;

    case clang::BO_LOr:
      InsertZ3Expr(c_op, Z3BoolCast(lhs) || Z3BoolCast(rhs));
      break;

    case clang::BO_EQ: {
      InsertZ3Expr(c_op, lhs == rhs);
    } break;

    case clang::BO_NE: {
      InsertZ3Expr(c_op, lhs != rhs);
    } break;

    case clang::BO_Rem:
      InsertZ3Expr(c_op, z3::srem(lhs, rhs));
      break;

    case clang::BO_Sub:
      InsertZ3Expr(c_op, lhs - rhs);
      break;

    default:
      LOG(FATAL) << "Unknown clang::BinaryOperator operation!";
      break;
  }
  return true;
}

// Translates clang variable references to Z3 constants.
bool Z3ConvVisitor::VisitDeclRefExpr(clang::DeclRefExpr *c_ref) {
  auto ref_decl = c_ref->getDecl();
  auto ref_name = ref_decl->getNameAsString();
  DLOG(INFO) << "VisitDeclRefExpr: " << ref_name;
  if (z3_expr_map.count(c_ref)) {
    return true;
  }

  auto z3_const = GetOrCreateZ3Decl(ref_decl);
  InsertZ3Expr(c_ref, z3_const());

  return true;
}

// Translates clang literals references to Z3 numeral values.
bool Z3ConvVisitor::VisitIntegerLiteral(clang::IntegerLiteral *c_lit) {
  auto lit_val = c_lit->getValue().getLimitedValue();
  DLOG(INFO) << "VisitIntegerLiteral: " << lit_val;
  if (z3_expr_map.count(c_lit)) {
    return true;
  }
  auto z3_sort = GetZ3Sort(c_lit->getType());
  if (z3_sort.is_bool()) {
    InsertZ3Expr(c_lit, z3_ctx->bool_val(lit_val != 0));
  } else {
    InsertZ3Expr(c_lit, z3_ctx->num_val(lit_val, z3_sort));
  }
  return true;
}

void Z3ConvVisitor::VisitZ3Expr(z3::expr z3_expr) {
  if (z3_expr.is_app()) {
    for (auto i = 0U; i < z3_expr.num_args(); ++i) {
      GetOrCreateCExpr(z3_expr.arg(i));
    }
    switch (z3_expr.decl().arity()) {
      case 0:
        VisitConstant(z3_expr);
        break;

      case 1:
        VisitUnaryApp(z3_expr);
        break;

      case 2:
        VisitBinaryApp(z3_expr);
        break;

      default:
        LOG(FATAL) << "Unexpected Z3 operation!";
        break;
    }
  } else if (z3_expr.is_quantifier()) {
    LOG(FATAL) << "Unexpected Z3 quantifier!";
  } else {
    LOG(FATAL) << "Unexpected Z3 variable!";
  }
}

void Z3ConvVisitor::VisitConstant(z3::expr z3_const) {
  DLOG(INFO) << "VisitConstant: " << z3_const;
  CHECK(z3_const.is_const()) << "Z3 expression is not a constant!";
  // Create C literals and variable references
  auto kind = z3_const.decl().decl_kind();
  clang::Expr *c_expr = nullptr;
  switch (kind) {
    // Boolean literals
    case Z3_OP_TRUE:
    case Z3_OP_FALSE:
    // Arithmetic numerals
    case Z3_OP_ANUM:
    // Bitvector numerals
    case Z3_OP_BNUM:
      c_expr = CreateLiteralExpr(z3_const);
      break;
    // Internal constants handled by parent Z3 exprs
    case Z3_OP_INTERNAL:
      break;
    // Uninterpreted constants
    case Z3_OP_UNINTERPRETED:
      c_expr = CreateDeclRefExpr(*ast_ctx, GetCValDecl(z3_const.decl()));
      break;

    // Unknowns
    default:
      LOG(FATAL) << "Unknown Z3 constant!";
      break;
  }
  InsertCExpr(z3_const, c_expr);
}

void Z3ConvVisitor::VisitUnaryApp(z3::expr z3_op) {
  DLOG(INFO) << "VisitUnaryApp: " << z3_op;
  CHECK(z3_op.is_app() && z3_op.decl().arity() == 1)
      << "Z3 expression is not a unary operator!";
  // Get operand
  auto operand = GetCExpr(z3_op.arg(0));
  // Get z3 function declaration
  auto z3_func = z3_op.decl();
  // Create C unary operator
  clang::Expr *c_op = nullptr;
  switch (z3_func.decl_kind()) {
    case Z3_OP_NOT:
      c_op = CreateNotExpr(*ast_ctx, operand);
      break;
    
    case Z3_OP_EXTRACT:
      LOG(FATAL) << "Unimplemented Z3_OP_EXTRACT";
      break;

    case Z3_OP_UNINTERPRETED: {
      auto name = z3_func.name().str();
      auto type = operand->getType();
      // Resolve opcode
      if (name == "AddrOf") {
        c_op = CreateUnaryOperator(*ast_ctx, clang::UO_AddrOf, operand,
                                   ast_ctx->getPointerType(type));
      } else if (name == "Deref") {
        c_op = CreateUnaryOperator(*ast_ctx, clang::UO_Deref, operand,
                                   type->getPointeeType());
      } else if (name == "Paren") {
        c_op = CreateParenExpr(*ast_ctx, operand);
      } else if (name == "PtrDecay") {
        c_op = CreateImplicitCastExpr(*ast_ctx, ast_ctx->getDecayedType(type),
                                      clang::CastKind::CK_ArrayToPointerDecay,
                                      operand);
      } else {
        LOG(FATAL) << "Unknown Z3 uninterpreted function";
      }
    } break;

    default:
      LOG(FATAL) << "Unknown Z3 unary operator!";
      break;
  }
  // Save
  InsertCExpr(z3_op, c_op);
}

void Z3ConvVisitor::VisitBinaryApp(z3::expr z3_op) {
  DLOG(INFO) << "VisitBinaryApp: " << z3_op;
  CHECK(z3_op.is_app() && z3_op.decl().arity() == 2)
      << "Z3 expression is not a binary operator!";
  // Get operands
  auto lhs = GetCExpr(z3_op.arg(0));
  // lhs->dump(llvm::errs());
  auto rhs = GetCExpr(z3_op.arg(1));
  // rhs->dump(llvm::errs());
  // Get result type
  auto type = GetQualType(z3_op.get_sort());
  // Get z3 function declaration
  auto z3_func = z3_op.decl();
  // Create C binary operator
  clang::Expr *c_op = nullptr;
  switch (z3_func.decl_kind()) {
    case Z3_OP_EQ:
      c_op = CreateBinaryOperator(*ast_ctx, clang::BO_EQ, lhs, rhs, type);
      break;

    case Z3_OP_AND: {
      c_op = lhs;
      for (auto i = 1U; i < z3_op.num_args(); ++i) {
        rhs = GetCExpr(z3_op.arg(i));
        c_op = CreateBinaryOperator(*ast_ctx, clang::BO_LAnd, c_op, rhs, type);
      }
    } break;

    case Z3_OP_OR: {
      c_op = lhs;
      for (auto i = 1U; i < z3_op.num_args(); ++i) {
        rhs = GetCExpr(z3_op.arg(i));
        c_op = CreateBinaryOperator(*ast_ctx, clang::BO_LOr, c_op, rhs, type);
      }
    } break;

    case Z3_OP_BSREM:
    case Z3_OP_BSREM_I:
      c_op = CreateBinaryOperator(*ast_ctx, clang::BO_Rem, lhs, rhs, type);
      break;

    case Z3_OP_UNINTERPRETED: {
      auto name = z3_func.name().str();
      // Resolve opcode
      if (name == "ArraySub") {
        c_op = CreateArraySubscriptExpr(*ast_ctx, lhs, rhs, type);
      } else if (name == "Member") {
        auto mem = GetCValDecl(z3_op.arg(1).decl());
        c_op = CreateMemberExpr(*ast_ctx, lhs, mem, type, /*is_arrow=*/true);
      } else {
        LOG(FATAL) << "Unknown Z3 uninterpreted function";
      }
    } break;

    // Unknowns
    default:
      LOG(FATAL) << "Unknown Z3 binary operator!";
      break;
  }
  // Save
  InsertCExpr(z3_op, c_op);
}

}  // namespace rellic