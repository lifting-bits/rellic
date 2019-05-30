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

    default:
      LOG(FATAL) << "Unknown Z3 sort!";
      break;
  }
}

static z3::sort GetZ3Sort(clang::ASTContext &ast_ctx, z3::context &z3_ctx,
                          clang::QualType type) {
  // Booleans
  if (type->isBooleanType()) {
    return z3_ctx.bool_sort();
  }
  auto bitwidth = ast_ctx.getTypeSize(type);
  // Floating points
  if (type->isRealFloatingType()) {
    switch (bitwidth) {
      case 16:
        return z3::to_sort(z3_ctx, Z3_mk_fpa_sort_16(z3_ctx));
        break;

      case 32:
        return z3::to_sort(z3_ctx, Z3_mk_fpa_sort_32(z3_ctx));
        break;

      case 64:
        return z3::to_sort(z3_ctx, Z3_mk_fpa_sort_64(z3_ctx));
        break;

      case 128:
        return z3::to_sort(z3_ctx, Z3_mk_fpa_sort_128(z3_ctx));
        break;

      default:
        LOG(FATAL) << "Unsupported floating-point bitwidth!";
        break;
    }
  }
  // Default to bitvectors
  return z3::to_sort(z3_ctx, Z3_mk_bv_sort(z3_ctx, bitwidth));
}

static clang::QualType GetQualType(clang::ASTContext &ctx, z3::sort z3_sort) {
  // Get sort size
  auto size = GetZ3SortSize(z3_sort);
  CHECK(size > 0) << "Type bit width has to be greater than 0!";
  // Determine C type for Z3 sort
  clang::QualType result;
  switch (z3_sort.sort_kind()) {
    // Bools
    case Z3_BOOL_SORT:
      result = ctx.BoolTy;
      break;
    // Bitvectors
    case Z3_BV_SORT:
      result = ctx.getIntTypeForBitwidth(size, 0);
      break;
    // IEEE 754 floating-points
    case Z3_FLOATING_POINT_SORT:
      result = ctx.getRealTypeForBitwidth(size);
      break;
    // Unknowns
    default:
      LOG(FATAL) << "Unknown Z3 sort!";
      break;
  }
  CHECK(!result.isNull()) << "Unknown C type for " << z3_sort;
  return result;
}

static clang::Expr *CreateLiteralExpr(clang::ASTContext &ast_ctx,
                                      z3::expr z3_expr) {
  DLOG(INFO) << "Creating literal clang::Expr for " << z3_expr;

  auto sort = z3_expr.get_sort();
  auto type = GetQualType(ast_ctx, sort);
  auto size = ast_ctx.getTypeSize(type);

  clang::Expr *result = nullptr;

  switch (sort.sort_kind()) {
    case Z3_BOOL_SORT: {
      type = ast_ctx.UnsignedIntTy;
      size = ast_ctx.getIntWidth(type);
      llvm::APInt val(size, z3_expr.bool_value() == Z3_L_TRUE ? 1 : 0);
      result = CreateIntegerLiteral(ast_ctx, val, type);
    } break;

    case Z3_BV_SORT: {
      llvm::APInt val(size, Z3_get_numeral_string(z3_expr.ctx(), z3_expr), 10);
      result = CreateIntegerLiteral(ast_ctx, val, type);
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
      result = CreateFloatingLiteral(ast_ctx, fval, type);
    } break;

    default:
      LOG(FATAL) << "Unknown Z3 sort!";
      break;
  }

  return result;
}

}  // namespace

Z3ConvVisitor::Z3ConvVisitor(clang::ASTContext *c_ctx, z3::context *z_ctx)
    : ast_ctx(c_ctx), z3_ctx(z_ctx), z3_expr_vec(*z3_ctx) {}

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

void Z3ConvVisitor::DeclareCVar(clang::ValueDecl *c_decl) {
  auto name = c_decl->getNameAsString();
  DLOG(INFO) << "Declaring C variable: " << name;
  CHECK(c_decl != nullptr);
  if (c_var_map.find(name) != c_var_map.end()) {
    DLOG(INFO) << "Re-declaration of " << name;
  }
  c_var_map[name] = c_decl;
}

clang::ValueDecl *Z3ConvVisitor::GetCVar(std::string var_name) {
  auto iter = c_var_map.find(var_name);
  CHECK(iter != c_var_map.end());
  return c_var_map[var_name];
}

// Retrieves or creates`z3::expr`s from `clang::Expr`.
z3::expr Z3ConvVisitor::GetOrCreateZ3Expr(clang::Expr *c_expr) {
  if (z3_expr_map.find(c_expr) == z3_expr_map.end()) {
    TraverseStmt(c_expr);
  }
  return GetZ3Expr(c_expr);
}

// Retrieves or creates `clang::Expr` from `z3::expr`.
clang::Expr *Z3ConvVisitor::GetOrCreateCExpr(z3::expr z3_expr) {
  if (c_expr_map.find(z3_expr.hash()) == c_expr_map.end()) {
    VisitZ3Expr(z3_expr);
  }
  return GetCExpr(z3_expr);
}

// Translates clang unary operators expressions to Z3 equivalents.
bool Z3ConvVisitor::VisitParenExpr(clang::ParenExpr *parens) {
  DLOG(INFO) << "VisitParenExpr";
  if (z3_expr_map.find(parens) == z3_expr_map.end()) {
    InsertZ3Expr(parens, GetOrCreateZ3Expr(parens->getSubExpr()));
  }
  return true;
}

// Translates clang unary operators expressions to Z3 equivalents.
bool Z3ConvVisitor::VisitUnaryOperator(clang::UnaryOperator *c_op) {
  DLOG(INFO) << "VisitUnaryOperator: "
             << c_op->getOpcodeStr(c_op->getOpcode()).str();
  if (z3_expr_map.find(c_op) == z3_expr_map.end()) {
    // Get operand
    auto operand = GetOrCreateZ3Expr(c_op->getSubExpr());
    // Create z3 unary op
    switch (c_op->getOpcode()) {
      case clang::UO_LNot:
        InsertZ3Expr(c_op, !Z3BoolCast(operand));
        break;

      default:
        LOG(FATAL) << "Unknown clang::UnaryOperator operation!";
        break;
    }
  }
  return true;
}

// Translates clang binary operators expressions to Z3 equivalents.
bool Z3ConvVisitor::VisitBinaryOperator(clang::BinaryOperator *c_op) {
  DLOG(INFO) << "VisitBinaryOperator: " << c_op->getOpcodeStr().str();
  if (z3_expr_map.find(c_op) == z3_expr_map.end()) {
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

      case clang::BO_EQ:
        InsertZ3Expr(c_op, lhs == rhs);
        break;

      case clang::BO_NE:
        InsertZ3Expr(c_op, lhs != rhs);
        break;

      case clang::BO_Rem:
        InsertZ3Expr(c_op, z3::srem(lhs, rhs));
        break;

      default:
        LOG(FATAL) << "Unknown clang::BinaryOperator operation!";
        break;
    }
  }
  return true;
}

// Translates clang variable references to Z3 constants.
bool Z3ConvVisitor::VisitDeclRefExpr(clang::DeclRefExpr *c_ref) {
  auto ref_decl = c_ref->getDecl();
  auto ref_name = ref_decl->getNameAsString();
  DLOG(INFO) << "VisitDeclRefExpr: " << ref_name;
  if (z3_expr_map.find(c_ref) == z3_expr_map.end()) {
    DeclareCVar(ref_decl);
    auto z3_sort = GetZ3Sort(*ast_ctx, *z3_ctx, c_ref->getType());
    auto z3_const = z3_ctx->constant(ref_name.c_str(), z3_sort);
    InsertZ3Expr(c_ref, z3_const);
  }
  return true;
}

// Translates clang literals references to Z3 numeral values.
bool Z3ConvVisitor::VisitIntegerLiteral(clang::IntegerLiteral *c_lit) {
  auto lit_val = c_lit->getValue().getLimitedValue();
  DLOG(INFO) << "VisitIntegerLiteral: " << lit_val;
  if (z3_expr_map.find(c_lit) == z3_expr_map.end()) {
    auto z3_sort = GetZ3Sort(*ast_ctx, *z3_ctx, c_lit->getType());
    if (z3_sort.is_bool()) {
      InsertZ3Expr(c_lit, z3_ctx->bool_val(lit_val != 0));
    } else {
      InsertZ3Expr(c_lit, z3_ctx->num_val(lit_val, z3_sort));
    }
  }
  return true;
}

void Z3ConvVisitor::VisitZ3Expr(z3::expr z3_expr) {
  if (z3_expr.is_app()) {
    for (unsigned i = 0; i < z3_expr.num_args(); ++i) {
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
      c_expr = CreateLiteralExpr(*ast_ctx, z3_const);
      break;
    // Uninterpreted constants
    case Z3_OP_UNINTERPRETED: {
      auto name = z3_const.decl().name().str();
      c_expr = CreateDeclRefExpr(*ast_ctx, GetCVar(name));
    } break;

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
  // Create C unary operator
  auto kind = z3_op.decl().decl_kind();
  clang::Expr *c_op = nullptr;
  switch (kind) {
    case Z3_OP_NOT:
      c_op = CreateNotExpr(*ast_ctx, operand);
      break;
    // Unknowns
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
  auto rhs = GetCExpr(z3_op.arg(1));
  // Get result type
  auto type = GetQualType(*ast_ctx, z3_op.get_sort());
  // Create C binary operator
  auto kind = z3_op.decl().decl_kind();
  clang::Expr *c_op = nullptr;
  switch (kind) {
    case Z3_OP_EQ:
      c_op = CreateBinaryOperator(*ast_ctx, clang::BO_EQ, lhs, rhs, type);
      break;

    case Z3_OP_AND: {
      c_op = lhs;
      for (unsigned i = 1; i < z3_op.num_args(); ++i) {
        rhs = GetCExpr(z3_op.arg(i));
        c_op = CreateBinaryOperator(*ast_ctx, clang::BO_LAnd, c_op, rhs, type);
      }
    } break;

    case Z3_OP_OR: {
      c_op = lhs;
      for (unsigned i = 1; i < z3_op.num_args(); ++i) {
        rhs = GetCExpr(z3_op.arg(i));
        c_op = CreateBinaryOperator(*ast_ctx, clang::BO_LOr, c_op, rhs, type);
      }
    } break;

    case Z3_OP_BSREM:
    case Z3_OP_BSREM_I:
      c_op = CreateBinaryOperator(*ast_ctx, clang::BO_Rem, lhs, rhs, type);
      break;

    // Unknowns
    default:
      LOG(FATAL) << "Unknown Z3 binary operator!";
      break;
  }
  // Save
  InsertCExpr(z3_op, c_op);
}

}  // namespace rellic