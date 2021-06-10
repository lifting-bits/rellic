/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#define GOOGLE_STRIP_LOG 1

#include "rellic/AST/Z3ConvVisitor.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "rellic/AST/Compat/ASTContext.h"

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
      auto &ctx{sort.ctx()};
      return Z3_fpa_get_sbits(ctx, sort) + Z3_fpa_get_ebits(ctx, sort);
    } break;

    case Z3_UNINTERPRETED_SORT:
      return 0;
      break;

    default:
      LOG(FATAL) << "Unknown Z3 sort: " << sort;
      break;
  }
  // This code is unreachable, but sometimes we need to
  // fix 'error: control reaches end of non-void function' on some compilers.
  return (unsigned)(-1);
}

static unsigned GetZ3SortSize(z3::expr expr) {
  return GetZ3SortSize(expr.get_sort());
}

// Determine if `op` is a `z3::concat(l, r)` that's
// equivalent to a sign extension. This is done by
// checking if `l` is an "all-one" or "all-zero" bit
// value.
static bool IsSignExt(z3::expr op) {
  if (op.decl().decl_kind() != Z3_OP_CONCAT) {
    return false;
  }

  auto lhs{op.arg(0)};

  if (lhs.is_numeral()) {
    auto size{GetZ3SortSize(lhs)};
    llvm::APInt val(size, Z3_get_numeral_string(op.ctx(), lhs), 10);
    return val.isAllOnesValue() || val.isNullValue();
  }
  return false;
}

static std::string CreateZ3DeclName(clang::NamedDecl *decl) {
  std::stringstream ss;
  ss << std::hex << decl << std::dec;
  ss << '_' << decl->getNameAsString();
  return ss.str();
}

}  // namespace

Z3ConvVisitor::Z3ConvVisitor(clang::ASTUnit &unit, z3::context *z_ctx)
    : c_ctx(&unit.getASTContext()),
      ast(unit),
      z_ctx(z_ctx),
      z_expr_vec(*z_ctx),
      z_decl_vec(*z_ctx) {}

// Inserts a `clang::Expr` <=> `z3::expr` mapping into
void Z3ConvVisitor::InsertZ3Expr(clang::Expr *c_expr, z3::expr z_expr) {
  CHECK(c_expr) << "Inserting null clang::Expr key.";
  CHECK(bool(z_expr)) << "Inserting null z3::expr value.";
  CHECK(!z_expr_map.count(c_expr)) << "clang::Expr key already exists.";
  z_expr_map[c_expr] = z_expr_vec.size();
  z_expr_vec.push_back(z_expr);
}

// Retrieves a `z3::expr` corresponding to `c_expr`.
// The `z3::expr` needs to be created and inserted by
// `Z3ConvVisistor::InsertZ3Expr` first.
z3::expr Z3ConvVisitor::GetZ3Expr(clang::Expr *c_expr) {
  auto iter{z_expr_map.find(c_expr)};
  CHECK(iter != z_expr_map.end());
  return z_expr_vec[iter->second];
}

// Inserts a `clang::ValueDecl` <=> `z3::func_decl` mapping into
void Z3ConvVisitor::InsertZ3Decl(clang::ValueDecl *c_decl,
                                 z3::func_decl z_decl) {
  CHECK(c_decl) << "Inserting null clang::ValueDecl key.";
  CHECK(bool(z_decl)) << "Inserting null z3::func_decl value.";
  CHECK(!z_decl_map.count(c_decl)) << "clang::ValueDecl key already exists.";
  z_decl_map[c_decl] = z_decl_vec.size();
  z_decl_vec.push_back(z_decl);
}

// Retrieves a `z3::func_decl` corresponding to `c_decl`.
// The `z3::func_decl` needs to be created and inserted by
// `Z3ConvVisistor::InsertZ3Decl` first.
z3::func_decl Z3ConvVisitor::GetZ3Decl(clang::ValueDecl *c_decl) {
  auto iter{z_decl_map.find(c_decl)};
  CHECK(iter != z_decl_map.end());
  return z_decl_vec[iter->second];
}

// If `expr` is not boolean, returns a `z3::expr` that corresponds
// to the non-boolean to boolean expression cast in C. Otherwise
// returns `expr`.
z3::expr Z3ConvVisitor::Z3BoolCast(z3::expr expr) {
  if (expr.is_bool()) {
    return expr;
  } else {
    return expr != z_ctx->num_val(0, expr.get_sort());
  }
}

z3::expr Z3ConvVisitor::Z3BoolToBVCast(z3::expr expr) {
  if (expr.is_bv()) {
    return expr;
  }

  CHECK(expr.is_bool());

  auto size{c_ctx->getTypeSize(c_ctx->IntTy)};
  return z3::ite(expr, z_ctx->bv_val(1U, size), z_ctx->bv_val(0U, size));
}

void Z3ConvVisitor::InsertCExpr(z3::expr z_expr, clang::Expr *c_expr) {
  CHECK(bool(z_expr)) << "Inserting null z3::expr key.";
  CHECK(c_expr) << "Inserting null clang::Expr value.";
  auto hash{z_expr.hash()};
  CHECK(!c_expr_map.count(hash)) << "z3::expr key already exists.";
  c_expr_map[hash] = c_expr;
}

clang::Expr *Z3ConvVisitor::GetCExpr(z3::expr z_expr) {
  auto hash{z_expr.hash()};
  CHECK(c_expr_map.count(hash)) << "No Z3 equivalent for C expression!";
  return c_expr_map[hash];
}

void Z3ConvVisitor::InsertCValDecl(z3::func_decl z_decl,
                                   clang::ValueDecl *c_decl) {
  CHECK(c_decl) << "Inserting null z3::func_decl key.";
  CHECK(bool(z_decl)) << "Inserting null clang::ValueDecl value.";
  CHECK(!c_decl_map.count(z_decl.id())) << "z3::func_decl key already exists.";
  c_decl_map[z_decl.id()] = c_decl;
}

clang::ValueDecl *Z3ConvVisitor::GetCValDecl(z3::func_decl z_decl) {
  CHECK(c_decl_map.count(z_decl.id())) << "No C equivalent for Z3 declaration!";
  return c_decl_map[z_decl.id()];
}

z3::sort Z3ConvVisitor::GetZ3Sort(clang::QualType type) {
  // Void
  if (type->isVoidType()) {
    return z_ctx->uninterpreted_sort("void");
  }
  // Booleans
  if (type->isBooleanType()) {
    return z_ctx->bool_sort();
  }
  // Structures
  if (type->isStructureType()) {
    auto decl{clang::cast<clang::RecordType>(type)->getDecl()};
    return z_ctx->uninterpreted_sort(decl->getNameAsString().c_str());
  }
  auto bitwidth{c_ctx->getTypeSize(type)};
  // Floating points
  if (type->isRealFloatingType()) {
    switch (bitwidth) {
      case 16:
        return z3::to_sort(*z_ctx, Z3_mk_fpa_sort_16(*z_ctx));
        break;

      case 32:
        return z3::to_sort(*z_ctx, Z3_mk_fpa_sort_32(*z_ctx));
        break;

      case 64:
        return z3::to_sort(*z_ctx, Z3_mk_fpa_sort_64(*z_ctx));
        break;

      case 128:
        return z3::to_sort(*z_ctx, Z3_mk_fpa_sort_128(*z_ctx));
        break;

      default:
        LOG(FATAL) << "Unsupported floating-point bitwidth!";
        break;
    }
  }
  // Default to bitvectors
  return z3::to_sort(*z_ctx, z_ctx->bv_sort(bitwidth));
}

clang::Expr *Z3ConvVisitor::CreateLiteralExpr(z3::expr z_expr) {
  DLOG(INFO) << "Creating literal clang::Expr for " << z_expr;
  CHECK(z_expr.is_const()) << "Can only create C literal from Z3 constant!";
  auto z_sort{z_expr.get_sort()};
  clang::Expr *result{nullptr};
  switch (z_sort.sort_kind()) {
    case Z3_BOOL_SORT: {
      auto val{z_expr.bool_value() == Z3_L_TRUE ? 1U : 0U};
      result = ast.CreateIntLit(llvm::APInt(/*BitWidth=*/1U, val));
    } break;

    case Z3_BV_SORT: {
      llvm::APInt val(GetZ3SortSize(z_expr),
                      Z3_get_numeral_string(z_expr.ctx(), z_expr), 10);
      // Handle `char` and `short` types separately, because clang
      // adds non-standard `Ui8` and `Ui16` suffixes respectively.
      result = ast.CreateAdjustedIntLit(val);
    } break;

    case Z3_FLOATING_POINT_SORT: {
      auto size{GetZ3SortSize(z_sort)};
      const llvm::fltSemantics *semantics{nullptr};
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
      z3::expr bv(*z_ctx, Z3_mk_fpa_to_ieee_bv(*z_ctx, z_expr));
      auto bits{Z3_get_numeral_string(*z_ctx, bv.simplify())};
      CHECK(std::strlen(bits) > 0)
          << "Failed to convert IEEE bitvector to string!";
      llvm::APInt api(size, bits, /*radix=*/10U);
      result = ast.CreateFPLit(llvm::APFloat(*semantics, api));
    } break;

    default:
      LOG(FATAL) << "Unknown Z3 sort: " << z_sort;
      break;
  }

  return result;
}

// Retrieves or creates`z3::expr`s from `clang::Expr`.
z3::expr Z3ConvVisitor::GetOrCreateZ3Expr(clang::Expr *c_expr) {
  if (!z_expr_map.count(c_expr)) {
    TraverseStmt(c_expr);
  }

  return GetZ3Expr(c_expr);
}

z3::func_decl Z3ConvVisitor::GetOrCreateZ3Decl(clang::ValueDecl *c_decl) {
  if (!z_decl_map.count(c_decl)) {
    TraverseDecl(c_decl);
  }

  auto z_decl{GetZ3Decl(c_decl)};
  if (!c_decl_map.count(z_decl.id())) {
    InsertCValDecl(z_decl, c_decl);
  }

  return z_decl;
}

// Retrieves or creates `clang::Expr` from `z3::expr`.
clang::Expr *Z3ConvVisitor::GetOrCreateCExpr(z3::expr z_expr) {
  if (!c_expr_map.count(z_expr.hash())) {
    VisitZ3Expr(z_expr);
  }
  return GetCExpr(z_expr);
}

// Retrieves or creates `clang::ValueDecl` from `z3::func_decl`.
clang::ValueDecl *Z3ConvVisitor::GetOrCreateCValDecl(z3::func_decl z_decl) {
  if (!c_decl_map.count(z_decl.id())) {
    VisitZ3Decl(z_decl);
  }
  return GetCValDecl(z_decl);
}

bool Z3ConvVisitor::VisitVarDecl(clang::VarDecl *c_var) {
  auto c_name{c_var->getNameAsString()};
  DLOG(INFO) << "VisitVarDecl: " << c_name;
  if (z_decl_map.count(c_var)) {
    DLOG(INFO) << "Re-declaration of " << c_name << "; Returning.";
    return true;
  }

  auto z_name{CreateZ3DeclName(c_var)};
  auto z_sort{GetZ3Sort(c_var->getType())};
  auto z_const{z_ctx->constant(z_name.c_str(), z_sort)};

  InsertZ3Decl(c_var, z_const.decl());

  return true;
}

bool Z3ConvVisitor::VisitFieldDecl(clang::FieldDecl *c_field) {
  auto c_name{c_field->getNameAsString()};
  DLOG(INFO) << "VisitFieldDecl: " << c_name;
  if (z_decl_map.count(c_field)) {
    DLOG(INFO) << "Re-declaration of " << c_name << "; Returning.";
    return true;
  }

  auto z_name{CreateZ3DeclName(c_field->getParent()) + "_" + c_name};
  auto z_sort{GetZ3Sort(c_field->getType())};
  auto z_const{z_ctx->constant(z_name.c_str(), z_sort)};

  InsertZ3Decl(c_field, z_const.decl());

  return true;
}

bool Z3ConvVisitor::VisitFunctionDecl(clang::FunctionDecl *c_func) {
  auto c_name{c_func->getNameAsString()};
  DLOG(INFO) << "VisitFunctionDecl: " << c_name;
  if (z_decl_map.count(c_func)) {
    DLOG(INFO) << "Re-declaration of " << c_name << "; Returning.";
    return true;
  }

  z3::sort_vector z_domains(*z_ctx);
  for (auto c_param : c_func->parameters()) {
    z_domains.push_back(GetZ3Sort(c_param->getType()));
  }

  auto z_range{GetZ3Sort(c_func->getReturnType())};
  auto z_func{z_ctx->function(c_name.c_str(), z_domains, z_range)};

  InsertZ3Decl(c_func, z_func);

  return true;
}

z3::expr Z3ConvVisitor::CreateZ3BitwiseCast(z3::expr expr, size_t src,
                                            size_t dst, bool sign) {
  if (expr.is_bool()) {
    expr = Z3BoolToBVCast(expr);
  }

  // CHECK(expr.is_bv()) << "z3::expr is not a bitvector!";
  CHECK_EQ(GetZ3SortSize(expr), src);

  int64_t diff = dst - src;
  // extend
  if (diff > 0) {
    return sign ? z3::sext(expr, diff) : z3::zext(expr, diff);
  }
  // truncate
  if (diff < 0) {
    return expr.extract(dst - 1, 0);
  }
  // nothing
  return expr;
}

template <typename T>
bool Z3ConvVisitor::HandleCastExpr(T *c_cast) {
  CHECK(clang::isa<clang::CStyleCastExpr>(c_cast) ||
        clang::isa<clang::ImplicitCastExpr>(c_cast));
  // C exprs
  auto c_sub{c_cast->getSubExpr()};
  // C types
  auto c_src_ty{c_sub->getType()};
  auto c_dst_ty{c_cast->getType()};
  // C type sizes
  auto src_ty_size{c_ctx->getTypeSize(c_src_ty)};
  auto dst_ty_size{c_ctx->getTypeSize(c_dst_ty)};
  // Z3 exprs
  auto z_sub{GetZ3Expr(c_sub)};
  auto z_cast{z_sub};
  // Z3 sorts
  auto z_src_sort{z_sub.get_sort()};
  auto z_dst_sort{z_ctx->bv_sort(dst_ty_size)};
  switch (c_cast->getCastKind()) {
    case clang::CastKind::CK_IntegralCast:
    case clang::CastKind::CK_NullToPointer:
      z_cast = CreateZ3BitwiseCast(z_sub, src_ty_size, dst_ty_size,
                                   c_src_ty->isSignedIntegerType());
      break;

    case clang::CastKind::CK_PointerToIntegral:
      z_cast = z_ctx->function("PtrToInt", z_src_sort, z_dst_sort)(z_sub);
      break;

    case clang::CastKind::CK_IntegralToPointer: {
      auto c_dst_ty_ptr{reinterpret_cast<uint64_t>(c_dst_ty.getAsOpaquePtr())};
      auto z_dst_ty_ptr{z_ctx->bv_val(c_dst_ty_ptr, 8 * sizeof(void *))};
      z_cast = z_ctx->function("IntToPtr", z_dst_ty_ptr.get_sort(), z_src_sort,
                               z_dst_sort)(z_dst_ty_ptr, z_sub);
    } break;

    case clang::CastKind::CK_BitCast: {
      auto c_dst_ty_ptr{reinterpret_cast<uint64_t>(c_dst_ty.getAsOpaquePtr())};
      auto z_dst_ty_ptr{z_ctx->bv_val(c_dst_ty_ptr, 8 * sizeof(void *))};
      auto z_dst_sort{z_ctx->bv_sort(dst_ty_size)};
      z_cast = z_ctx->function("BitCast", z_dst_ty_ptr.get_sort(), z_src_sort,
                               z_dst_sort)(z_dst_ty_ptr, z_sub);
    } break;

    case clang::CastKind::CK_ArrayToPointerDecay: {
      CHECK(z_sub.is_bv()) << "Pointer cast operand is not a bit-vector";
      auto z_ptr_sort{GetZ3Sort(c_cast->getType())};
      auto z_arr_sort{z_sub.get_sort()};
      z_cast = z_ctx->function("PtrDecay", z_arr_sort, z_ptr_sort)(z_sub);
    } break;

    case clang::CastKind::CK_NoOp:
    case clang::CastKind::CK_LValueToRValue:
    case clang::CastKind::CK_FunctionToPointerDecay:
      // case clang::CastKind::CK_ArrayToPointerDecay:
      break;

    default:
      LOG(FATAL) << "Unsupported cast type: " << c_cast->getCastKindName();
      break;
  }
  // Save
  InsertZ3Expr(c_cast, z_cast);

  return true;
}

bool Z3ConvVisitor::VisitCStyleCastExpr(clang::CStyleCastExpr *c_cast) {
  DLOG(INFO) << "VisitCStyleCastExpr";
  if (z_expr_map.count(c_cast)) {
    return true;
  }
  return HandleCastExpr<clang::CStyleCastExpr>(c_cast);
}

bool Z3ConvVisitor::VisitImplicitCastExpr(clang::ImplicitCastExpr *c_cast) {
  DLOG(INFO) << "VisitImplicitCastExpr";
  if (z_expr_map.count(c_cast)) {
    return true;
  }
  return HandleCastExpr<clang::ImplicitCastExpr>(c_cast);
}

bool Z3ConvVisitor::VisitArraySubscriptExpr(clang::ArraySubscriptExpr *sub) {
  DLOG(INFO) << "VisitArraySubscriptExpr";
  if (z_expr_map.count(sub)) {
    return true;
  }
  // Get base
  auto z_base{GetZ3Expr(sub->getBase())};
  auto base_sort{z_base.get_sort()};
  CHECK(base_sort.is_bv()) << "Invalid Z3 sort for base expression";
  // Get index
  auto z_idx{GetZ3Expr(sub->getIdx())};
  auto idx_sort{z_idx.get_sort()};
  CHECK(idx_sort.is_bv()) << "Invalid Z3 sort for index expression";
  // Get result
  auto elm_sort{GetZ3Sort(sub->getType())};
  // Create a z_function
  auto z_arr_sub{z_ctx->function("ArraySub", base_sort, idx_sort, elm_sort)};
  // Create a z3 expression
  InsertZ3Expr(sub, z_arr_sub(z_base, z_idx));
  // Done
  return true;
}

bool Z3ConvVisitor::VisitMemberExpr(clang::MemberExpr *expr) {
  DLOG(INFO) << "VisitMemberExpr";
  if (z_expr_map.count(expr)) {
    return true;
  }

  auto z_mem{GetOrCreateZ3Decl(expr->getMemberDecl())()};
  auto z_base{GetZ3Expr(expr->getBase())};
  auto z_mem_expr{z_ctx->function("Member", z_base.get_sort(), z_mem.get_sort(),
                                  z_mem.get_sort())};

  InsertZ3Expr(expr, z_mem_expr(z_base, z_mem));

  return true;
}

bool Z3ConvVisitor::VisitCallExpr(clang::CallExpr *c_call) {
  DLOG(INFO) << "VisitCallExpr";
  if (z_expr_map.count(c_call)) {
    return true;
  }
  z3::expr_vector z_args(*z_ctx);
  // Get call id
  llvm::FoldingSetNodeID c_call_id;
  c_call->Profile(c_call_id, *c_ctx, /*Canonical=*/true);
  z_args.push_back(z_ctx->bv_val(c_call_id.ComputeHash(), /*sz=*/64U));
  // Get callee
  auto z_callee{GetZ3Expr(c_call->getCallee())};
  z_args.push_back(z_callee);
  // Get call arguments
  for (auto c_arg : c_call->arguments()) {
    z_args.push_back(GetZ3Expr(c_arg));
  }
  // Build the z3 call
  z3::sort_vector z_domain(*z_ctx);
  for (auto z_arg : z_args) {
    z_domain.push_back(z_arg.get_sort());
  }
  auto z_range{z_callee.is_array() ? z_callee.get_sort().array_range()
                                   : z_callee.get_sort()};
  auto z_call{z_ctx->function("Call", z_domain, z_range)};
  // Insert the call
  InsertZ3Expr(c_call, z_call(z_args));

  return true;
}

// Translates clang unary operators expressions to Z3 equivalents.
bool Z3ConvVisitor::VisitParenExpr(clang::ParenExpr *parens) {
  DLOG(INFO) << "VisitParenExpr";
  if (z_expr_map.count(parens)) {
    return true;
  }

  auto z_sub{GetZ3Expr(parens->getSubExpr())};

  switch (z_sub.decl().decl_kind()) {
    // Parens may affect semantics of C expressions
    case Z3_OP_UNINTERPRETED: {
      auto sort{z_sub.get_sort()};
      auto z_paren{z_ctx->function("Paren", sort, sort)};
      InsertZ3Expr(parens, z_paren(z_sub));
    } break;
    // Default to ignoring the parens, Z3 should know how
    // to interpret them.
    default:
      InsertZ3Expr(parens, z_sub);
      break;
  }

  return true;
}

// Translates clang unary operators expressions to Z3 equivalents.
bool Z3ConvVisitor::VisitUnaryOperator(clang::UnaryOperator *c_op) {
  DLOG(INFO) << "VisitUnaryOperator: "
             << c_op->getOpcodeStr(c_op->getOpcode()).str();
  if (z_expr_map.count(c_op)) {
    return true;
  }
  // Get operand
  auto operand{GetZ3Expr(c_op->getSubExpr())};
  // Conditionally cast operands to a bitvector
  auto CondBoolToBVCast{[this, &operand]() {
    if (operand.is_bool()) {
      operand = Z3BoolToBVCast(operand);
    }
  }};
  // Create z3 unary op
  switch (c_op->getOpcode()) {
    case clang::UO_LNot:
      InsertZ3Expr(c_op, !Z3BoolCast(operand));
      break;

    case clang::UO_Minus:
      InsertZ3Expr(c_op, -operand);
      break;

    case clang::UO_Not:
      CondBoolToBVCast();
      InsertZ3Expr(c_op, ~operand);
      break;

    case clang::UO_AddrOf: {
      auto ptr_sort{GetZ3Sort(c_op->getType())};
      auto z_addrof{z_ctx->function("AddrOf", operand.get_sort(), ptr_sort)};
      InsertZ3Expr(c_op, z_addrof(operand));
    } break;

    case clang::UO_Deref: {
      auto elm_sort{GetZ3Sort(c_op->getType())};
      auto z_deref{z_ctx->function("Deref", operand.get_sort(), elm_sort)};
      InsertZ3Expr(c_op, z_deref(operand));
    } break;

    default:
      LOG(FATAL) << "Unknown clang::UnaryOperator operation: "
                 << c_op->getOpcodeStr(c_op->getOpcode()).str();
      break;
  }
  return true;
}

// Translates clang binary operators expressions to Z3 equivalents.
bool Z3ConvVisitor::VisitBinaryOperator(clang::BinaryOperator *c_op) {
  DLOG(INFO) << "VisitBinaryOperator: " << c_op->getOpcodeStr().str();
  if (z_expr_map.count(c_op)) {
    return true;
  }
  // Get operands
  auto lhs{GetZ3Expr(c_op->getLHS())};
  auto rhs{GetZ3Expr(c_op->getRHS())};
  // Conditionally cast operands match size to the wider one
  auto CondSizeCast{[this, &lhs, &rhs] {
    auto lhs_size{GetZ3SortSize(lhs)};
    auto rhs_size{GetZ3SortSize(rhs)};
    if (lhs_size < rhs_size) {
      lhs = CreateZ3BitwiseCast(lhs, lhs_size, rhs_size, /*sign=*/true);
    } else if (lhs_size > rhs_size) {
      rhs = CreateZ3BitwiseCast(rhs, rhs_size, lhs_size, /*sign=*/true);
    }
  }};
  // Conditionally cast operands to bool
  auto CondBoolCast{[this, &lhs, &rhs]() {
    if (lhs.is_bool() || rhs.is_bool()) {
      lhs = Z3BoolCast(lhs);
      rhs = Z3BoolCast(rhs);
    }
  }};
  // Conditionally cast operands to a bitvector
  auto CondBoolToBVCast{[this, &lhs, &rhs]() {
    if (lhs.is_bool()) {
      CHECK(rhs.is_bv());
      lhs = Z3BoolToBVCast(lhs);
    }
    if (rhs.is_bool()) {
      CHECK(lhs.is_bv());
      rhs = Z3BoolToBVCast(rhs);
    }
  }};
  // Create z3 binary op
  switch (c_op->getOpcode()) {
    case clang::BO_LAnd:
      CondBoolCast();
      InsertZ3Expr(c_op, lhs && rhs);
      break;

    case clang::BO_LOr:
      CondBoolCast();
      InsertZ3Expr(c_op, lhs || rhs);
      break;

    case clang::BO_EQ: {
      CondBoolCast();
      InsertZ3Expr(c_op, lhs == rhs);
    } break;

    case clang::BO_NE:
      CondBoolCast();
      InsertZ3Expr(c_op, lhs != rhs);
      break;

    case clang::BO_GE:
      InsertZ3Expr(c_op, lhs >= rhs);
      break;

    case clang::BO_GT:
      InsertZ3Expr(c_op, lhs > rhs);
      break;

    case clang::BO_LE:
      InsertZ3Expr(c_op, lhs <= rhs);
      break;

    case clang::BO_LT:
      InsertZ3Expr(c_op, lhs < rhs);
      break;

    case clang::BO_Rem:
      InsertZ3Expr(c_op, z3::srem(lhs, rhs));
      break;

    case clang::BO_Add:
      InsertZ3Expr(c_op, lhs + rhs);
      break;

    case clang::BO_Sub:
      InsertZ3Expr(c_op, lhs - rhs);
      break;

    case clang::BO_Mul:
      InsertZ3Expr(c_op, lhs * rhs);
      break;

    case clang::BO_Div:
      InsertZ3Expr(c_op, lhs / rhs);
      break;

    case clang::BO_And:
      CondBoolToBVCast();
      InsertZ3Expr(c_op, lhs & rhs);
      break;

    case clang::BO_Or:
      CondBoolToBVCast();
      InsertZ3Expr(c_op, lhs | rhs);
      break;

    case clang::BO_Xor:
      InsertZ3Expr(c_op, lhs ^ rhs);
      break;

    case clang::BO_Shr:
      CondBoolToBVCast();
      CondSizeCast();
      InsertZ3Expr(c_op, c_op->getLHS()->getType()->isSignedIntegerType()
                             ? z3::ashr(lhs, rhs)
                             : z3::lshr(lhs, rhs));
      break;

    case clang::BO_Shl:
      CondSizeCast();
      InsertZ3Expr(c_op, z3::shl(lhs, rhs));
      break;

    default:
      LOG(FATAL) << "Unknown clang::BinaryOperator operation: "
                 << c_op->getOpcodeStr().str();
      break;
  }
  return true;
}

bool Z3ConvVisitor::VisitConditionalOperator(clang::ConditionalOperator *c_op) {
  DLOG(INFO) << "VisitConditionalOperator";
  if (z_expr_map.count(c_op)) {
    return true;
  }

  auto z_cond{GetZ3Expr(c_op->getCond())};
  auto z_then{GetZ3Expr(c_op->getTrueExpr())};
  auto z_else{GetZ3Expr(c_op->getFalseExpr())};
  InsertZ3Expr(c_op, z3::ite(z_cond, z_then, z_else));

  return true;
}

// Translates clang variable references to Z3 constants.
bool Z3ConvVisitor::VisitDeclRefExpr(clang::DeclRefExpr *c_ref) {
  auto c_ref_decl{c_ref->getDecl()};
  auto c_ref_name{c_ref_decl->getNameAsString()};
  DLOG(INFO) << "VisitDeclRefExpr: " << c_ref_name;
  if (z_expr_map.count(c_ref)) {
    return true;
  }

  auto z_decl{GetOrCreateZ3Decl(c_ref_decl)};
  auto z_ref{z_decl.is_const() ? z_decl() : z3::as_array(z_decl)};

  InsertZ3Expr(c_ref, z_ref);

  return true;
}

// Translates clang character literals references to Z3 numeral values.
bool Z3ConvVisitor::VisitCharacterLiteral(clang::CharacterLiteral *c_lit) {
  auto c_val{c_lit->getValue()};
  DLOG(INFO) << "VisitCharacterLiteral: " << c_val;
  if (z_expr_map.count(c_lit)) {
    return true;
  }

  auto z_sort{GetZ3Sort(c_lit->getType())};
  auto z_val{z_ctx->num_val(c_val, z_sort)};
  InsertZ3Expr(c_lit, z_val);

  return true;
}

// Translates clang integer literal references to Z3 numeral values.
bool Z3ConvVisitor::VisitIntegerLiteral(clang::IntegerLiteral *c_lit) {
  auto c_val{c_lit->getValue().getLimitedValue()};
  DLOG(INFO) << "VisitIntegerLiteral: " << c_val;
  if (z_expr_map.count(c_lit)) {
    return true;
  }

  auto z_sort{GetZ3Sort(c_lit->getType())};
  auto z_val{z_sort.is_bool() ? z_ctx->bool_val(c_val != 0)
                              : z_ctx->num_val(c_val, z_sort)};
  InsertZ3Expr(c_lit, z_val);

  return true;
}

// Translates clang floating point literal references to Z3 numeral values.
bool Z3ConvVisitor::VisitFloatingLiteral(clang::FloatingLiteral *lit) {
  auto api{lit->getValue().bitcastToAPInt()};
  DLOG(INFO) << "VisitFloatingLiteral: " << api.bitsToDouble();
  if (z_expr_map.count(lit)) {
    return true;
  }

  auto size{api.getBitWidth()};
  auto bits{api.toString(/*Radix=*/10, /*Signed=*/false)};
  auto sort{GetZ3Sort(lit->getType())};
  auto bv{z_ctx->bv_val(bits.c_str(), size)};
  auto fpa{z3::to_expr(*z_ctx, Z3_mk_fpa_to_fp_bv(*z_ctx, bv, sort))};

  InsertZ3Expr(lit, fpa);

  return true;
}

void Z3ConvVisitor::VisitZ3Expr(z3::expr z_expr) {
  auto z_decl{z_expr.decl()};
  CHECK(z_expr.is_app()) << "Unexpected Z3 operation: " << z_decl.name();
  // Handle arguments first
  for (auto i{0U}; i < z_expr.num_args(); ++i) {
    GetOrCreateCExpr(z_expr.arg(i));
  }
  // TODO(msurovic): Rework this into a visitor based on
  // z_expr.decl().decl_kind()
  // Handle bitvector concats
  if (z_decl.decl_kind() == Z3_OP_CONCAT) {
    InsertCExpr(z_expr, HandleZ3Concat(z_expr));
    return;
  }
  // Handle calls to user functions
  if (z_decl.decl_kind() == Z3_OP_UNINTERPRETED &&
      z_decl.name().str() == "Call") {
    InsertCExpr(z_expr, HandleZ3Call(z_expr));
    return;
  }
  // Handle the rest
  switch (z_decl.arity()) {
    case 0:
      VisitConstant(z_expr);
      break;

    case 1:
      VisitUnaryApp(z_expr);
      break;

    case 2:
      VisitBinaryApp(z_expr);
      break;

    case 3:
      VisitTernaryApp(z_expr);
      break;

    default:
      LOG(FATAL) << "Unexpected Z3 operation: " << z_decl.name();
      break;
  }
}

void Z3ConvVisitor::VisitConstant(z3::expr z_const) {
  DLOG(INFO) << "VisitConstant: " << z_const;
  CHECK(z_const.is_const()) << "Z3 expression is not a constant!";
  // Create C literals and variable references
  clang::Expr *c_expr{nullptr};
  switch (z_const.decl().decl_kind()) {
    // Boolean literals
    case Z3_OP_TRUE:
    case Z3_OP_FALSE:
    // Arithmetic numerals
    case Z3_OP_ANUM:
    // Bitvector numerals
    case Z3_OP_BNUM:
    // Floating-point numerals
    case Z3_OP_FPA_NUM:
    case Z3_OP_FPA_PLUS_INF:
    case Z3_OP_FPA_MINUS_INF:
    case Z3_OP_FPA_NAN:
    case Z3_OP_FPA_PLUS_ZERO:
    case Z3_OP_FPA_MINUS_ZERO:
      c_expr = CreateLiteralExpr(z_const);
      break;
    // Functions-as-array expressions
    case Z3_OP_AS_ARRAY: {
      z3::func_decl z_decl(*z_ctx, Z3_get_as_array_func_decl(*z_ctx, z_const));
      c_expr = ast.CreateDeclRef(GetOrCreateCValDecl(z_decl));
    } break;
    // Uninterpreted constants
    case Z3_OP_UNINTERPRETED: {
      auto c_decl{GetOrCreateCValDecl(z_const.decl())};
      if (clang::isa<clang::FieldDecl>(c_decl)) {
        c_expr = ast.CreateNull();
      } else {
        c_expr = ast.CreateDeclRef(c_decl);
      }
    } break;
    // Internal constants handled by parent Z3 exprs
    case Z3_OP_INTERNAL:
      break;
    // Unknowns
    default:
      LOG(FATAL) << "Unknown Z3 constant: " << z_const;
      break;
  }
  InsertCExpr(z_const, c_expr);
}

clang::Expr *Z3ConvVisitor::HandleZ3Concat(z3::expr z_op) {
  auto lhs{GetCExpr(z_op.arg(0U))};
  for (auto i{1U}; i < z_op.num_args(); ++i) {
    // Given a `(concat l r)` we generate `((t)l << w) | r` where
    //  * `w` is the bitwidth of `r`
    //
    //  * `t` is the smallest integer type that can fit the result
    //        of `(concat l r)`
    auto rhs{GetCExpr(z_op.arg(i))};
    auto res_ty{ast.GetLeastIntTypeForBitWidth(GetZ3SortSize(z_op),
                                               /*sign=*/0U)};
    if (!IsSignExt(z_op)) {
      auto cast{ast.CreateCStyleCast(res_ty, lhs)};
      auto shl_val{
          ast.CreateIntLit(llvm::APInt(32U, GetZ3SortSize(z_op.arg(1U))))};
      auto shl{ast.CreateShl(cast, shl_val)};
      auto bor{ast.CreateOr(shl, rhs)};
      lhs = ast.CreateParen(bor);
    } else {
      lhs = ast.CreateCStyleCast(res_ty, rhs);
    }
  }
  return lhs;
}

clang::Expr *Z3ConvVisitor::HandleZ3Call(z3::expr z_op) {
  auto c_callee{GetCExpr(z_op.arg(1U))};
  std::vector<clang::Expr *> c_args;
  for (auto i{2U}; i < z_op.num_args(); ++i) {
    c_args.push_back(GetCExpr(z_op.arg(i)));
  }
  return ast.CreateCall(c_callee, c_args);
}

void Z3ConvVisitor::VisitUnaryApp(z3::expr z_op) {
  DLOG(INFO) << "VisitUnaryApp: " << z_op;
  CHECK(z_op.is_app() && z_op.decl().arity() == 1)
      << "Z3 expression is not a unary operator!";
  // Get operand
  auto c_sub{GetCExpr(z_op.arg(0U))};
  // Get z3 function declaration
  auto z_decl{z_op.decl()};
  // Create C unary operator
  clang::Expr *c_op{nullptr};
  switch (z_decl.decl_kind()) {
    case Z3_OP_NOT:
      c_op = ast.CreateLNot(ast.CreateParen(c_sub));
      break;

    case Z3_OP_BNOT:
      c_op = ast.CreateNot(ast.CreateParen(c_sub));
      break;
    // Given a `(extract hi lo o)` we generate `(o >> lo & m)` where:
    //
    //  * `o`   is the operand from which we extract a bit sequence
    //
    //  * `hi`  is the upper bound of the extracted sequence
    //
    //  * `lo`  is the lower bound of the extracted sequence
    //
    //  * `m`   is a bitmask integer literal
    case Z3_OP_EXTRACT: {
      if (z_op.lo() != 0U) {
        auto shr_val{ast.CreateIntLit(llvm::APInt(32U, z_op.lo()))};
        auto shr{ast.CreateShr(c_sub, shr_val)};
        auto mask_val{ast.CreateIntLit(
            llvm::APInt::getAllOnesValue(GetZ3SortSize(z_op)))};
        c_op = ast.CreateParen(ast.CreateAnd(shr, mask_val));
      } else {
        c_op = ast.CreateCStyleCast(
            ast.GetLeastIntTypeForBitWidth(GetZ3SortSize(z_op),
                                           /*sign=*/0),
            c_sub);
      }

    } break;

    case Z3_OP_SIGN_EXT:
      c_op = ast.CreateCStyleCast(
          ast.GetLeastIntTypeForBitWidth(GetZ3SortSize(z_op),
                                         /*sign=*/1),
          ast.CreateParen(c_sub));
      break;

    case Z3_OP_UNINTERPRETED: {
      // Resolve opcode
      auto z_func_name{z_decl.name().str()};
      if (z_func_name == "AddrOf") {
        c_op = ast.CreateAddrOf(c_sub);
      } else if (z_func_name == "Deref") {
        c_op = ast.CreateDeref(c_sub);
      } else if (z_func_name == "Paren") {
        c_op = ast.CreateParen(c_sub);
      } else if (z_func_name == "PtrToInt") {
        auto s_size{GetZ3SortSize(z_op)};
        auto t_op{ast.GetLeastIntTypeForBitWidth(s_size, /*sign=*/0U)};
        c_op = ast.CreateCStyleCast(t_op, c_sub);
      } else if (z_func_name == "PtrDecay") {
        c_op = c_sub;
      } else {
        LOG(FATAL) << "Unknown Z3 uninterpreted unary function: "
                   << z_func_name;
      }
    } break;

    default:
      LOG(FATAL) << "Unknown Z3 unary operator: " << z_decl.name().str();
      break;
  }
  // Save
  InsertCExpr(z_op, c_op);
}

void Z3ConvVisitor::VisitBinaryApp(z3::expr z_op) {
  DLOG(INFO) << "VisitBinaryApp: " << z_op;
  CHECK(z_op.is_app() && z_op.decl().arity() == 2)
      << "Z3 expression is not a binary operator!";
  // Get operands
  auto lhs{GetCExpr(z_op.arg(0U))};
  auto rhs{GetCExpr(z_op.arg(1U))};
  // Get result type for casts
  auto GetTypeFromOpaquePtrLiteral{[&lhs] {
    auto c_lit{clang::cast<clang::IntegerLiteral>(lhs)};
    auto t_dst_opaque_ptr_val{c_lit->getValue().getLimitedValue()};
    auto t_dst_opaque_ptr{reinterpret_cast<void *>(t_dst_opaque_ptr_val)};
    return clang::QualType::getFromOpaquePtr(t_dst_opaque_ptr);
  }};
  // Get z3 function declaration
  auto z_decl{z_op.decl()};
  // Create C binary operator
  clang::Expr *c_op{nullptr};
  switch (z_decl.decl_kind()) {
    // `&&` in z3 can be n-ary, so we create a tree of C binary `&&`.
    case Z3_OP_AND: {
      c_op = lhs;
      for (auto i{1U}; i < z_op.num_args(); ++i) {
        rhs = GetCExpr(z_op.arg(i));
        c_op = ast.CreateLAnd(c_op, rhs);
      }
    } break;
    // `||` in z3 can be n-ary, so we create a tree of C binary `||`.
    case Z3_OP_OR: {
      c_op = lhs;
      for (auto i{1U}; i < z_op.num_args(); ++i) {
        rhs = GetCExpr(z_op.arg(i));
        c_op = ast.CreateLOr(c_op, rhs);
      }
    } break;

    case Z3_OP_EQ:
      c_op = ast.CreateEQ(lhs, rhs);
      break;

    case Z3_OP_SLEQ:
      c_op = ast.CreateLE(lhs, rhs);
      break;

    case Z3_OP_FPA_LT:
      c_op = ast.CreateLT(lhs, rhs);
      break;

    case Z3_OP_BADD:
      c_op = ast.CreateAdd(lhs, rhs);
      break;

    case Z3_OP_BLSHR:
      c_op = ast.CreateShr(lhs, rhs);
      break;

    case Z3_OP_BASHR: {
      auto size{c_ctx->getTypeSize(lhs->getType())};
      auto type{ast.GetLeastIntTypeForBitWidth(size, /*sign=*/1U)};
      auto cast{ast.CreateCStyleCast(type, lhs)};
      c_op = ast.CreateShr(cast, rhs);
    } break;

    case Z3_OP_BSHL:
      c_op = ast.CreateShl(lhs, rhs);
      break;

    case Z3_OP_BOR:
      c_op = ast.CreateOr(lhs, rhs);
      break;

    case Z3_OP_BXOR:
      c_op = ast.CreateXor(lhs, rhs);
      break;

    case Z3_OP_BMUL:
      c_op = ast.CreateMul(lhs, rhs);
      break;

    case Z3_OP_BSDIV:
    case Z3_OP_BSDIV_I:
      c_op = ast.CreateDiv(lhs, rhs);
      break;

    case Z3_OP_BSREM:
    case Z3_OP_BSREM_I:
      c_op = ast.CreateRem(lhs, rhs);
      break;

    case Z3_OP_UNINTERPRETED: {
      auto name{z_decl.name().str()};
      // Resolve opcode
      if (name == "ArraySub") {
        c_op = ast.CreateArraySub(lhs, rhs);
      } else if (name == "Member") {
        auto mem{GetOrCreateCValDecl(z_op.arg(1).decl())};
        auto field{clang::dyn_cast<clang::FieldDecl>(mem)};
        CHECK(field != nullptr) << "Operand is not a clang::FieldDecl";
        c_op = ast.CreateDot(lhs, field);
      } else if (name == "IntToPtr" || name == "BitCast") {
        c_op = ast.CreateCStyleCast(GetTypeFromOpaquePtrLiteral(), rhs);
      } else {
        LOG(FATAL) << "Unknown Z3 uninterpreted binary function: " << name;
      }
    } break;

    // Unknowns
    default:
      LOG(FATAL) << "Unknown Z3 binary operator: " << z_decl.name().str();
      break;
  }
  // Save
  InsertCExpr(z_op, c_op);
}

void Z3ConvVisitor::VisitTernaryApp(z3::expr z_op) {
  DLOG(INFO) << "VisitTernaryApp: " << z_op;
  CHECK(z_op.is_app() && z_op.decl().arity() == 3)
      << "Z3 expression is not a ternary operator!";
  // Create C binary operator
  auto z_decl{z_op.decl()};
  switch (z_decl.decl_kind()) {
    case Z3_OP_ITE: {
      auto c_cond{GetCExpr(z_op.arg(0U))};
      auto c_then{GetCExpr(z_op.arg(1U))};
      auto c_else{GetCExpr(z_op.arg(2U))};
      InsertCExpr(z_op, ast.CreateConditional(c_cond, c_then, c_else));
    } break;
    // Unknowns
    default:
      LOG(FATAL) << "Unknown Z3 ternary operator: " << z_decl.name();
      break;
  }
}

void Z3ConvVisitor::VisitZ3Decl(z3::func_decl z_decl) {
  LOG(FATAL) << "Unimplemented Z3 declaration visitor!";
  // switch (z_decl.decl_kind()) {
  //   case Z3_OP_UNINTERPRETED:
  //     if (!z_decl.is_const()) {

  //     } else {
  //       LOG(FATAL) << "Unimplemented Z3 declaration!";
  //     }
  //     break;

  //   default:
  //     LOG(FATAL) << "Unknown Z3 function declaration: " << z_decl.name();
  //     break;
  // }
}

}  // namespace rellic