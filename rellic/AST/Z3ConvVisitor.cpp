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
#include "rellic/AST/Util.h"

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
    llvm::APInt val(size, Z3_get_numeral_string(op.ctx(), op), 10);
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

Z3ConvVisitor::Z3ConvVisitor(clang::ASTContext *c_ctx, z3::context *z3_ctx)
    : ast_ctx(c_ctx),
      ast(*c_ctx),
      z3_ctx(z3_ctx),
      z3_expr_vec(*z3_ctx),
      z3_decl_vec(*z3_ctx) {}

// Inserts a `clang::Expr` <=> `z3::expr` mapping into
void Z3ConvVisitor::InsertZ3Expr(clang::Expr *c_expr, z3::expr z_expr) {
  CHECK(c_expr) << "Inserting null clang::Expr key.";
  CHECK(bool(z_expr)) << "Inserting null z3::expr value.";
  CHECK(!z3_expr_map.count(c_expr)) << "clang::Expr key already exists.";
  z3_expr_map[c_expr] = z3_expr_vec.size();
  z3_expr_vec.push_back(z_expr);
}

// Retrieves a `z3::expr` corresponding to `c_expr`.
// The `z3::expr` needs to be created and inserted by
// `Z3ConvVisistor::InsertZ3Expr` first.
z3::expr Z3ConvVisitor::GetZ3Expr(clang::Expr *c_expr) {
  auto iter{z3_expr_map.find(c_expr)};
  CHECK(iter != z3_expr_map.end());
  return z3_expr_vec[iter->second];
}

// Inserts a `clang::ValueDecl` <=> `z3::func_decl` mapping into
void Z3ConvVisitor::InsertZ3Decl(clang::ValueDecl *c_decl,
                                 z3::func_decl z_decl) {
  CHECK(c_decl) << "Inserting null clang::ValueDecl key.";
  CHECK(bool(z_decl)) << "Inserting null z3::func_decl value.";
  CHECK(!z3_decl_map.count(c_decl)) << "clang::ValueDecl key already exists.";
  z3_decl_map[c_decl] = z3_decl_vec.size();
  z3_decl_vec.push_back(z_decl);
}

// Retrieves a `z3::func_decl` corresponding to `c_decl`.
// The `z3::func_decl` needs to be created and inserted by
// `Z3ConvVisistor::InsertZ3Decl` first.
z3::func_decl Z3ConvVisitor::GetZ3Decl(clang::ValueDecl *c_decl) {
  auto iter{z3_decl_map.find(c_decl)};
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
    auto cast{expr != z3_ctx->num_val(0, expr.get_sort())};
    return cast.simplify();
  }
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
  CHECK(c_expr_map.count(hash)) << "No Z3 equivalent for C declaration!";
  return c_expr_map[hash];
}

void Z3ConvVisitor::InsertCValDecl(z3::func_decl z_decl,
                                   clang::ValueDecl *c_decl) {
  CHECK(c_decl) << "Inserting null z3::func_decl key.";
  CHECK(bool(z_decl)) << "Inserting null clang::ValueDecl value.";
  auto id{Z3_get_func_decl_id(*z3_ctx, z_decl)};
  CHECK(!c_decl_map.count(id)) << "z3::func_decl key already exists.";
  c_decl_map[id] = c_decl;
}

clang::ValueDecl *Z3ConvVisitor::GetCValDecl(z3::func_decl z_decl) {
  auto id{Z3_get_func_decl_id(*z3_ctx, z_decl)};
  CHECK(c_decl_map.count(id)) << "No C equivalent for Z3 declaration!";
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
    return z3_ctx->uninterpreted_sort(decl->getNameAsString().c_str());
  }
  auto bitwidth = ast_ctx->getTypeSize(type);
  // Floating points
  if (type->isRealFloatingType()) {
    switch (bitwidth) {
      case 16:
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

clang::Expr *Z3ConvVisitor::CreateLiteralExpr(z3::expr z_expr) {
  DLOG(INFO) << "Creating literal clang::Expr for " << z_expr;

  auto sort = z_expr.get_sort();

  clang::Expr *result = nullptr;

  switch (sort.sort_kind()) {
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
      auto size{GetZ3SortSize(sort)};
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
      z3::expr bv(*z3_ctx, Z3_mk_fpa_to_ieee_bv(*z3_ctx, z_expr));
      auto bits{Z3_get_numeral_string(*z3_ctx, bv.simplify())};
      CHECK(std::strlen(bits) > 0)
          << "Failed to convert IEEE bitvector to string!";
      llvm::APInt api(size, bits, /*radix=*/10U);
      result = ast.CreateFPLit(llvm::APFloat(*semantics, api));
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

  auto z_decl = GetZ3Decl(c_decl);

  auto id = Z3_get_func_decl_id(*z3_ctx, z_decl);
  if (!c_decl_map.count(id)) {
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

bool Z3ConvVisitor::VisitVarDecl(clang::VarDecl *var) {
  auto name = var->getNameAsString();
  DLOG(INFO) << "VisitVarDecl: " << name;
  if (z3_decl_map.count(var)) {
    DLOG(INFO) << "Re-declaration of " << name << "; Returning.";
    return true;
  }

  auto z_name = CreateZ3DeclName(var);
  auto z_sort = GetZ3Sort(var->getType());
  auto z_const = z3_ctx->constant(z_name.c_str(), z_sort);

  InsertZ3Decl(var, z_const.decl());

  return true;
}

bool Z3ConvVisitor::VisitFieldDecl(clang::FieldDecl *field) {
  auto name = field->getNameAsString();
  DLOG(INFO) << "VisitFieldDecl: " << name;
  if (z3_decl_map.count(field)) {
    DLOG(INFO) << "Re-declaration of " << name << "; Returning.";
    return true;
  }

  auto z_name = CreateZ3DeclName(field->getParent()) + "_" + name;
  auto z_sort = GetZ3Sort(field->getType());
  auto z_const = z3_ctx->constant(z_name.c_str(), z_sort);

  InsertZ3Decl(field, z_const.decl());

  return true;
}

bool Z3ConvVisitor::VisitFunctionDecl(clang::FunctionDecl *func) {
  DLOG(INFO) << "VisitFunctionDecl";
  LOG(FATAL) << "Unimplemented FunctionDecl visitor";
  return true;
}

z3::expr Z3ConvVisitor::CreateZ3BitwiseCast(z3::expr expr, size_t src,
                                            size_t dst, bool sign) {
  if (expr.is_bool()) {
    auto s_src{z3_ctx->bool_sort()};
    auto s_dst{z3_ctx->bv_sort(ast_ctx->getTypeSize(ast_ctx->IntTy))};
    expr = z3_ctx->function("BoolToBV", s_src, s_dst)(expr);
  }

  CHECK(expr.is_bv()) << "z3::expr is not a bitvector!";
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

bool Z3ConvVisitor::VisitCStyleCastExpr(clang::CStyleCastExpr *c_cast) {
  DLOG(INFO) << "VisitCStyleCastExpr";
  if (z3_expr_map.count(c_cast)) {
    return true;
  }
  // C exprs
  auto c_sub = c_cast->getSubExpr();
  // C types
  auto t_src = c_sub->getType();
  auto t_dst = c_cast->getType();
  // C type sizes
  auto t_src_size = ast_ctx->getTypeSize(t_src);
  auto t_dst_size = ast_ctx->getTypeSize(t_dst);
  // Z3 exprs
  auto z_sub = GetOrCreateZ3Expr(c_sub);
  auto z_cast = CreateZ3BitwiseCast(z_sub, t_src_size, t_dst_size,
                                    t_src->isSignedIntegerType());

  switch (c_cast->getCastKind()) {
    case clang::CastKind::CK_PointerToIntegral: {
      auto s_src{z_sub.get_sort()};
      auto s_dst{z_cast.get_sort()};
      z_cast = z3_ctx->function("PtrToInt", s_src, s_dst)(z_sub);
    } break;

    case clang::CastKind::CK_IntegralToPointer: {
      auto s_src{z_sub.get_sort()};
      auto s_dst{z_cast.get_sort()};
      auto t_dst_opaque_ptr_val{
          reinterpret_cast<uint64_t>(t_dst.getAsOpaquePtr())};
      auto z_ptr{z3_ctx->bv_val(t_dst_opaque_ptr_val, 8 * sizeof(void *))};
      auto s_ptr{z_ptr.get_sort()};
      z_cast = z3_ctx->function("IntToPtr", s_ptr, s_src, s_dst)(z_ptr, z_sub);
    } break;

    case clang::CastKind::CK_IntegralCast:
    case clang::CastKind::CK_NullToPointer:
      break;

    case clang::CastKind::CK_BitCast: {
      auto s_src{z_sub.get_sort()};
      auto s_dst{z_cast.get_sort()};
      auto t_dst_opaque_ptr_val{
          reinterpret_cast<uint64_t>(t_dst.getAsOpaquePtr())};
      auto z_ptr{z3_ctx->bv_val(t_dst_opaque_ptr_val, 8 * sizeof(void *))};
      auto s_ptr{z_ptr.get_sort()};
      z_cast = z3_ctx->function("BitCast", s_ptr, s_src, s_dst)(z_ptr, z_sub);
    } break;

    default:
      LOG(FATAL) << "Unsupported cast type: " << c_cast->getCastKindName();
      break;
  }

  // Save
  InsertZ3Expr(c_cast, z_cast);

  return true;
}

bool Z3ConvVisitor::VisitImplicitCastExpr(clang::ImplicitCastExpr *c_cast) {
  DLOG(INFO) << "VisitImplicitCastExpr";
  if (z3_expr_map.count(c_cast)) {
    return true;
  }

  auto c_sub = c_cast->getSubExpr();
  auto z_sub = GetOrCreateZ3Expr(c_sub);

  switch (c_cast->getCastKind()) {
    case clang::CastKind::CK_ArrayToPointerDecay: {
      CHECK(z_sub.is_bv()) << "Pointer cast operand is not a bit-vector";
      auto s_ptr = GetZ3Sort(c_cast->getType());
      auto s_arr = z_sub.get_sort();
      auto z_func = z3_ctx->function("PtrDecay", s_arr, s_ptr);
      InsertZ3Expr(c_cast, z_func(z_sub));
    } break;

    default:
      LOG(FATAL) << "Unsupported cast type: " << c_cast->getCastKindName();
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
  auto z_base = GetOrCreateZ3Expr(sub->getBase());
  auto base_sort = z_base.get_sort();
  CHECK(base_sort.is_bv()) << "Invalid Z3 sort for base expression";
  // Get index
  auto z_idx = GetOrCreateZ3Expr(sub->getIdx());
  auto idx_sort = z_idx.get_sort();
  CHECK(idx_sort.is_bv()) << "Invalid Z3 sort for index expression";
  // Get result
  auto elm_sort = GetZ3Sort(sub->getType());
  // Create a z_function
  auto z_arr_sub = z3_ctx->function("ArraySub", base_sort, idx_sort, elm_sort);
  // Create a z3 expression
  InsertZ3Expr(sub, z_arr_sub(z_base, z_idx));
  // Done
  return true;
}

bool Z3ConvVisitor::VisitMemberExpr(clang::MemberExpr *expr) {
  DLOG(INFO) << "VisitMemberExpr";
  if (z3_expr_map.count(expr)) {
    return true;
  }

  auto z_mem = GetOrCreateZ3Decl(expr->getMemberDecl())();
  auto z_base = GetOrCreateZ3Expr(expr->getBase());
  auto z_mem_expr = z3_ctx->function("Member", z_base.get_sort(),
                                     z_mem.get_sort(), z_mem.get_sort());

  InsertZ3Expr(expr, z_mem_expr(z_base, z_mem));

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

  auto z_sub = GetOrCreateZ3Expr(parens->getSubExpr());

  switch (z_sub.decl().decl_kind()) {
    // Parens may affect semantics of C expressions
    case Z3_OP_UNINTERPRETED: {
      auto sort{z_sub.get_sort()};
      auto z_paren{z3_ctx->function("Paren", sort, sort)};
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
  if (z3_expr_map.count(c_op)) {
    return true;
  }
  // Get operand
  auto operand = GetOrCreateZ3Expr(c_op->getSubExpr());
  // Create z3 unary op
  switch (c_op->getOpcode()) {
    case clang::UO_LNot:
      InsertZ3Expr(c_op, !Z3BoolCast(operand));
      break;

    case clang::UO_Minus:
      InsertZ3Expr(c_op, -operand);
      break;

    case clang::UO_AddrOf: {
      auto ptr_sort = GetZ3Sort(c_op->getType());
      auto z_addrof = z3_ctx->function("AddrOf", operand.get_sort(), ptr_sort);
      InsertZ3Expr(c_op, z_addrof(operand));
    } break;

    case clang::UO_Deref: {
      auto elm_sort = GetZ3Sort(c_op->getType());
      auto z_deref = z3_ctx->function("Deref", operand.get_sort(), elm_sort);
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
  if (z3_expr_map.count(c_op)) {
    return true;
  }
  // Get operands
  auto lhs{GetOrCreateZ3Expr(c_op->getLHS())};
  auto rhs{GetOrCreateZ3Expr(c_op->getRHS())};
  // Conditionally cast operands to bool
  auto CondBoolCast{[this, &lhs, &rhs]() {
    if (lhs.is_bool() || rhs.is_bool()) {
      lhs = Z3BoolCast(lhs);
      rhs = Z3BoolCast(rhs);
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
      InsertZ3Expr(c_op, lhs & rhs);
      break;

    case clang::BO_Or:
      InsertZ3Expr(c_op, lhs | rhs);
      break;

    case clang::BO_Xor:
      CondBoolCast();
      InsertZ3Expr(c_op, lhs ^ rhs);
      break;

    case clang::BO_Shr:
      InsertZ3Expr(c_op, c_op->getLHS()->getType()->isSignedIntegerType()
                             ? z3::ashr(lhs, rhs)
                             : z3::lshr(lhs, rhs));
      break;

    case clang::BO_Shl:
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
  if (z3_expr_map.count(c_op)) {
    return true;
  }

  auto z3_cond{GetOrCreateZ3Expr(c_op->getCond())};
  auto z3_then{GetOrCreateZ3Expr(c_op->getTrueExpr())};
  auto z3_else{GetOrCreateZ3Expr(c_op->getFalseExpr())};
  InsertZ3Expr(c_op, z3::ite(z3_cond, z3_then, z3_else));

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

  auto z_const = GetOrCreateZ3Decl(ref_decl);
  InsertZ3Expr(c_ref, z_const());

  return true;
}

// Translates clang character literals references to Z3 numeral values.
bool Z3ConvVisitor::VisitCharacterLiteral(clang::CharacterLiteral *c_lit) {
  auto c_val = c_lit->getValue();
  DLOG(INFO) << "VisitCharacterLiteral: " << c_val;
  if (z3_expr_map.count(c_lit)) {
    return true;
  }

  auto z_sort = GetZ3Sort(c_lit->getType());
  auto z_val = z3_ctx->num_val(c_val, z_sort);
  InsertZ3Expr(c_lit, z_val);

  return true;
}

// Translates clang integer literal references to Z3 numeral values.
bool Z3ConvVisitor::VisitIntegerLiteral(clang::IntegerLiteral *c_lit) {
  auto c_val = c_lit->getValue().getLimitedValue();
  DLOG(INFO) << "VisitIntegerLiteral: " << c_val;
  if (z3_expr_map.count(c_lit)) {
    return true;
  }

  auto z_sort = GetZ3Sort(c_lit->getType());
  auto z_val = z_sort.is_bool() ? z3_ctx->bool_val(c_val != 0)
                                : z3_ctx->num_val(c_val, z_sort);
  InsertZ3Expr(c_lit, z_val);

  return true;
}

// Translates clang floating point literal references to Z3 numeral values.
bool Z3ConvVisitor::VisitFloatingLiteral(clang::FloatingLiteral *lit) {
  auto api{lit->getValue().bitcastToAPInt()};
  DLOG(INFO) << "VisitFloatingLiteral: " << api.bitsToDouble();
  if (z3_expr_map.count(lit)) {
    return true;
  }

  auto size{api.getBitWidth()};
  auto bits{api.toString(/*Radix=*/10, /*Signed=*/false)};
  auto sort{GetZ3Sort(lit->getType())};
  auto bv{z3_ctx->bv_val(bits.c_str(), size)};
  auto fpa{z3::to_expr(*z3_ctx, Z3_mk_fpa_to_fp_bv(*z3_ctx, bv, sort))};

  InsertZ3Expr(lit, fpa);

  return true;
}

void Z3ConvVisitor::VisitZ3Expr(z3::expr z_expr) {
  if (z_expr.is_app()) {
    for (auto i = 0U; i < z_expr.num_args(); ++i) {
      GetOrCreateCExpr(z_expr.arg(i));
    }
    switch (z_expr.decl().arity()) {
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
        LOG(FATAL) << "Unexpected Z3 operation!";
        break;
    }
  } else if (z_expr.is_quantifier()) {
    LOG(FATAL) << "Unexpected Z3 quantifier!";
  } else {
    LOG(FATAL) << "Unexpected Z3 variable!";
  }
}

void Z3ConvVisitor::VisitConstant(z3::expr z_const) {
  DLOG(INFO) << "VisitConstant: " << z_const;
  CHECK(z_const.is_const()) << "Z3 expression is not a constant!";
  // Create C literals and variable references
  auto kind{z_const.decl().decl_kind()};
  clang::Expr *c_expr{nullptr};
  switch (kind) {
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
    // Internal constants handled by parent Z3 exprs
    case Z3_OP_INTERNAL:
      break;
    // Uninterpreted constants
    case Z3_OP_UNINTERPRETED:
      c_expr = CreateDeclRefExpr(*ast_ctx, GetCValDecl(z_const.decl()));
      break;

    // Unknowns
    default:
      LOG(FATAL) << "Unknown Z3 constant: " << z_const;
      break;
  }
  InsertCExpr(z_const, c_expr);
}

void Z3ConvVisitor::VisitUnaryApp(z3::expr z_op) {
  DLOG(INFO) << "VisitUnaryApp: " << z_op;
  CHECK(z_op.is_app() && z_op.decl().arity() == 1)
      << "Z3 expression is not a unary operator!";
  // Get operand
  auto c_sub = GetCExpr(z_op.arg(0));
  auto t_sub = c_sub->getType();
  // Get z3 function declaration
  auto z_func = z_op.decl();
  // Create C unary operator
  clang::Expr *c_op{nullptr};
  switch (z_func.decl_kind()) {
    case Z3_OP_NOT:
      c_op = CreateNotExpr(*ast_ctx, c_sub);
      break;
    // Given a `(extract hi lo o)` we generate `((o & m) >> lo)` where:
    //
    //  * `o`   is the operand from which we extract a bit sequence
    //
    //  * `hi`  is the upper bound of the extracted sequence
    //
    //  * `lo`  is the lower bound of the extracted sequence
    //
    //  * `m`   is a bitmask integer literal
    case Z3_OP_EXTRACT: {
      if (z_op.lo() != 0) {
        auto t_uint{ast_ctx->UnsignedIntTy};
        auto t_uint_size{ast_ctx->getTypeSize(t_uint)};
        auto t_res{ast_ctx->getIntegerTypeOrder(t_sub, t_uint) < 0 ? t_uint
                                                                   : t_sub};
        // Shift value
        auto c_shift_val{llvm::APInt(t_uint_size, z_op.lo())};
        // Shift literal
        auto c_shift_lit{ast.CreateIntLit(c_shift_val)};
        // Mask value
        auto c_mask_val{
            llvm::APInt::getBitsSet(t_uint_size, z_op.lo(), z_op.hi() + 1)};
        // Mask literal
        auto c_mask_lit{ast.CreateIntLit(c_mask_val)};
        // And
        auto c_and{CreateBinaryOperator(
            *ast_ctx, clang::BO_And, CastExpr(*ast_ctx, t_res, c_sub),
            CastExpr(*ast_ctx, t_res, c_mask_lit), t_res)};
        // LShr
        c_sub = CreateBinaryOperator(
            *ast_ctx, clang::BO_Shr, CreateParenExpr(*ast_ctx, c_and),
            CastExpr(*ast_ctx, t_res, c_shift_lit), t_res);
      }
      c_op = CastExpr(
          *ast_ctx,
          GetLeastIntTypeForBitWidth(*ast_ctx, GetZ3SortSize(z_op), /*sign=*/0),
          CreateParenExpr(*ast_ctx, c_sub));
    } break;

    case Z3_OP_UNINTERPRETED: {
      // Resolve opcode
      auto z_func_name{z_func.name().str()};
      if (z_func_name == "AddrOf") {
        auto t_op = ast_ctx->getPointerType(t_sub);
        c_op = CreateUnaryOperator(*ast_ctx, clang::UO_AddrOf, c_sub, t_op);
      } else if (z_func_name == "Deref") {
        CHECK(t_sub->isPointerType()) << "Deref operand type is not a pointer";
        auto t_op = t_sub->getPointeeType();
        c_op = CreateUnaryOperator(*ast_ctx, clang::UO_Deref, c_sub, t_op);
      } else if (z_func_name == "Paren") {
        c_op = CreateParenExpr(*ast_ctx, c_sub);
      } else if (z_func_name == "PtrDecay") {
        CHECK(t_sub->isArrayType()) << "PtrDecay operand type is not an array";
        auto t_op = ast_ctx->getArrayDecayedType(t_sub);
        c_op = CreateImplicitCastExpr(
            *ast_ctx, t_op, clang::CastKind::CK_ArrayToPointerDecay, c_sub);
      } else if (z_func_name == "PtrToInt") {
        auto s_size = GetZ3SortSize(z_op);
        auto t_op = ast_ctx->getIntTypeForBitwidth(s_size, /*sign=*/0);
        c_op = CreateCStyleCastExpr(
            *ast_ctx, t_op, clang::CastKind::CK_PointerToIntegral, c_sub);
      } else if (z_func_name == "BoolToBV") {
        c_op = c_sub;
      } else {
        LOG(FATAL) << "Unknown Z3 uninterpreted unary function: "
                   << z_func_name;
      }
    } break;

    default:
      LOG(FATAL) << "Unknown Z3 unary operator!";
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
  auto lhs{GetCExpr(z_op.arg(0))};
  auto rhs{GetCExpr(z_op.arg(1))};
  // Get result type for integers
  auto GetIntResultType{[this, &lhs, &rhs] {
    auto lht{lhs->getType()};
    auto rht{rhs->getType()};
    auto order{ast_ctx->getIntegerTypeOrder(lht, rht)};
    return order < 0 ? rht : lht;
  }};
  // Convenience wrapper
  auto BinOpExpr{[this, lhs, rhs](clang::BinaryOperatorKind opc,
                                  clang::QualType type) {
    return CreateBinaryOperator(*ast_ctx, opc,
                                CastExpr(*ast_ctx, rhs->getType(), lhs),
                                CastExpr(*ast_ctx, lhs->getType(), rhs), type);
  }};
  // Get result type for casts
  auto GetTypeFromOpaquePtrLiteral{[&lhs] {
    auto c_lit{clang::cast<clang::IntegerLiteral>(lhs)};
    auto t_dst_opaque_ptr_val{c_lit->getValue().getLimitedValue()};
    auto t_dst_opaque_ptr{reinterpret_cast<void *>(t_dst_opaque_ptr_val)};
    return clang::QualType::getFromOpaquePtr(t_dst_opaque_ptr);
  }};
  // Get z3 function declaration
  auto z_func{z_op.decl()};
  // Create C binary operator
  clang::Expr *c_op{nullptr};
  switch (z_func.decl_kind()) {
    // `&&` in z3 can be n-ary, so we create a tree of C binary `&&`.
    case Z3_OP_AND: {
      c_op = lhs;
      for (auto i = 1U; i < z_op.num_args(); ++i) {
        rhs = GetCExpr(z_op.arg(i));
        c_op = CreateBinaryOperator(*ast_ctx, clang::BO_LAnd, c_op, rhs,
                                    ast_ctx->IntTy);
      }
    } break;
    // `||` in z3 can be n-ary, so we create a tree of C binary `||`.
    case Z3_OP_OR: {
      c_op = lhs;
      for (auto i = 1U; i < z_op.num_args(); ++i) {
        rhs = GetCExpr(z_op.arg(i));
        c_op = CreateBinaryOperator(*ast_ctx, clang::BO_LOr, c_op, rhs,
                                    ast_ctx->IntTy);
      }
    } break;

    case Z3_OP_EQ:
      c_op = BinOpExpr(clang::BO_EQ, ast_ctx->IntTy);
      break;

    case Z3_OP_SLEQ:
      c_op = BinOpExpr(clang::BO_LE, ast_ctx->IntTy);
      break;

    case Z3_OP_FPA_LT:
      c_op = BinOpExpr(clang::BO_LT, ast_ctx->IntTy);
      break;

    // Given a `(concat l r)` we generate `((t)l << w) | r` where
    //  * `w` is the bitwidth of `r`
    //
    //  * `t` is the smallest integer type that can fit the result
    //        of `(concat l r)`
    case Z3_OP_CONCAT: {
      auto t_res{GetLeastIntTypeForBitWidth(*ast_ctx, GetZ3SortSize(z_op),
                                            /*sign=*/0)};
      if (!IsSignExt(z_op)) {
        auto t_uint{ast_ctx->UnsignedIntTy};
        auto t_uint_size{ast_ctx->getTypeSize(t_uint)};

        auto c_cast{CastExpr(*ast_ctx, t_res, lhs)};

        auto c_shift_val{llvm::APInt(
            t_uint_size, ast_ctx->getTypeSize(lhs->getType()), /*isSigned=*/0)};

        auto c_shift_lit{ast.CreateIntLit(c_shift_val)};

        auto c_shift{CreateBinaryOperator(
            *ast_ctx, clang::BO_Shl, CastExpr(*ast_ctx, t_uint, c_cast),
            CastExpr(*ast_ctx, t_res, c_shift_lit),
            ast_ctx->getIntegerTypeOrder(t_res, t_uint) < 0 ? t_uint : t_res)};

        auto c_or{CreateBinaryOperator(
            *ast_ctx, clang::BO_Or,
            CastExpr(*ast_ctx, rhs->getType(),
                     CreateParenExpr(*ast_ctx, c_shift)),
            CastExpr(*ast_ctx, c_shift->getType(), rhs),
            ast_ctx->getIntegerTypeOrder(c_shift->getType(), rhs->getType()) < 0
                ? rhs->getType()
                : c_shift->getType())};
        c_op = CreateParenExpr(*ast_ctx, c_or);
      } else {
        c_op = CastExpr(*ast_ctx, t_res, rhs);
      }
    } break;

    case Z3_OP_BADD:
      c_op = BinOpExpr(clang::BO_Add, GetIntResultType());
      break;

    case Z3_OP_BASHR:
      c_op = BinOpExpr(clang::BO_Shr, GetIntResultType());
      break;

    case Z3_OP_BXOR:
      c_op = BinOpExpr(clang::BO_Xor, GetIntResultType());
      break;

    case Z3_OP_BMUL:
      c_op = BinOpExpr(clang::BO_Mul, GetIntResultType());
      break;

    case Z3_OP_BSDIV:
    case Z3_OP_BSDIV_I:
      c_op = BinOpExpr(clang::BO_Div, GetIntResultType());
      break;

    case Z3_OP_BSREM:
    case Z3_OP_BSREM_I:
      c_op = BinOpExpr(clang::BO_Rem, GetIntResultType());
      break;

    case Z3_OP_UNINTERPRETED: {
      auto name = z_func.name().str();
      // Resolve opcode
      if (name == "ArraySub") {
        auto base_type = lhs->getType()->getAs<clang::PointerType>();
        CHECK(base_type) << "Operand is not a clang::PointerType";
        c_op = CreateArraySubscriptExpr(*ast_ctx, lhs, rhs,
                                        base_type->getPointeeType());
      } else if (name == "Member") {
        auto mem = GetCValDecl(z_op.arg(1).decl());
        c_op = CreateMemberExpr(*ast_ctx, lhs, mem, mem->getType(),
                                /*is_arrow=*/false);
      } else if (name == "IntToPtr") {
        c_op = CreateCStyleCastExpr(*ast_ctx, GetTypeFromOpaquePtrLiteral(),
                                    clang::CastKind::CK_IntegralToPointer, rhs);
      } else if (name == "BitCast") {
        c_op = CreateCStyleCastExpr(*ast_ctx, GetTypeFromOpaquePtrLiteral(),
                                    clang::CastKind::CK_BitCast, rhs);
      } else {
        LOG(FATAL) << "Unknown Z3 uninterpreted binary function: " << name;
      }
    } break;

    // Unknowns
    default:
      LOG(FATAL) << "Unknown Z3 binary operator: " << z_func.name().str();
      break;
  }
  // Save
  InsertCExpr(z_op, c_op);
}

void Z3ConvVisitor::VisitTernaryApp(z3::expr z_op) {
  DLOG(INFO) << "VisitTernaryApp: " << z_op;
  CHECK(z_op.is_app() && z_op.decl().arity() == 3)
      << "Z3 expression is not a ternary operator!";
  // Get Z3 function declaration
  auto z_func{z_op.decl()};
  // Create C binary operator
  clang::Expr *c_op{nullptr};
  switch (z_func.decl_kind()) {
    case Z3_OP_ITE: {
      auto c_cond{GetCExpr(z_op.arg(0))};
      auto c_then{GetCExpr(z_op.arg(1))};
      auto c_else{GetCExpr(z_op.arg(2))};
      c_op = CreateConditionalOperatorExpr(*ast_ctx, c_cond, c_then, c_else,
                                           c_then->getType());
      c_op = CreateParenExpr(*ast_ctx, c_op);
    } break;
    // Unknowns
    default:
      LOG(FATAL) << "Unknown Z3 binary operator!";
      break;
  }
  // Save
  InsertCExpr(z_op, c_op);
}

}  // namespace rellic