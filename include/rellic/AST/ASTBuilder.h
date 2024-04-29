/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/Basic/Builtins.h>
#include <clang/Frontend/ASTUnit.h>

#include <string>

namespace clang {
class Sema;
}  // namespace clang

namespace rellic {

class ASTBuilder {
 private:
  clang::ASTUnit &unit;
  clang::ASTContext &ctx;
  clang::Sema &sema;

 public:
  ASTBuilder(clang::ASTUnit &unit);
  // Type helpers
  clang::QualType GetLeastIntTypeForBitWidth(unsigned size, unsigned sign);
  clang::QualType GetLeastRealTypeForBitWidth(unsigned size);
  // Literals
  clang::IntegerLiteral *CreateIntLit(llvm::APSInt val);

  clang::IntegerLiteral *CreateIntLit(llvm::APInt val) {
    return CreateIntLit(llvm::APSInt(val, /*isUnsigned=*/true));
  };

  clang::IntegerLiteral *CreateTrue() {
    return CreateIntLit(llvm::APInt(/*numBits=*/1U, /*val*/ 1U));
  };

  clang::IntegerLiteral *CreateFalse() {
    return CreateIntLit(llvm::APInt(/*numBits=*/1U, /*val*/ 0U));
  };

  clang::CharacterLiteral *CreateCharLit(llvm::APInt val);
  clang::CharacterLiteral *CreateCharLit(unsigned val);
  clang::StringLiteral *CreateStrLit(std::string val);
  clang::Expr *CreateFPLit(llvm::APFloat val);
  // Special values
  clang::Expr *CreateNull();
  clang::Expr *CreateUndefPointer(clang::QualType type);
  clang::Expr *CreateUndefInteger(clang::QualType type);
  // Identifiers
  clang::IdentifierInfo *CreateIdentifier(std::string name);
  // Variable declaration
  clang::VarDecl *CreateVarDecl(
      clang::DeclContext *decl_ctx, clang::QualType type,
      clang::IdentifierInfo *id,
      clang::StorageClass storage_class = clang::SC_None);

  clang::VarDecl *CreateVarDecl(
      clang::DeclContext *decl_ctx, clang::QualType type, std::string name,
      clang::StorageClass storage_class = clang::SC_None) {
    return CreateVarDecl(decl_ctx, type, CreateIdentifier(name), storage_class);
  }
  // Function declaration
  clang::FunctionDecl *CreateFunctionDecl(clang::DeclContext *decl_ctx,
                                          clang::QualType type,
                                          clang::IdentifierInfo *id);

  clang::FunctionDecl *CreateFunctionDecl(clang::DeclContext *decl_ctx,
                                          clang::QualType type,
                                          std::string name) {
    return CreateFunctionDecl(decl_ctx, type, CreateIdentifier(name));
  }
  // Function parameter declaration
  clang::ParmVarDecl *CreateParamDecl(clang::DeclContext *decl_ctx,
                                      clang::QualType type,
                                      clang::IdentifierInfo *id);

  clang::ParmVarDecl *CreateParamDecl(clang::DeclContext *decl_ctx,
                                      clang::QualType type, std::string name) {
    return CreateParamDecl(decl_ctx, type, CreateIdentifier(name));
  }
  // Structure declaration
  clang::RecordDecl *CreateStructDecl(clang::DeclContext *decl_ctx,
                                      clang::IdentifierInfo *id,
                                      clang::RecordDecl *prev_decl = nullptr);

  clang::RecordDecl *CreateStructDecl(clang::DeclContext *decl_ctx,
                                      std::string name,
                                      clang::RecordDecl *prev_decl = nullptr) {
    return CreateStructDecl(decl_ctx, CreateIdentifier(name), prev_decl);
  }
  // Union declaration
  clang::RecordDecl *CreateUnionDecl(clang::DeclContext *decl_ctx,
                                     clang::IdentifierInfo *id,
                                     clang::RecordDecl *prev_decl = nullptr);

  clang::RecordDecl *CreateUnionDecl(clang::DeclContext *decl_ctx,
                                     std::string name,
                                     clang::RecordDecl *prev_decl = nullptr) {
    return CreateUnionDecl(decl_ctx, CreateIdentifier(name), prev_decl);
  }
  // Enum declaration
  clang::EnumDecl *CreateEnumDecl(clang::DeclContext *decl_ctx,
                                  clang::IdentifierInfo *id,
                                  clang::EnumDecl *prev_decl = nullptr);

  clang::EnumDecl *CreateEnumDecl(clang::DeclContext *decl_ctx,
                                  std::string name,
                                  clang::EnumDecl *prev_decl = nullptr) {
    return CreateEnumDecl(decl_ctx, CreateIdentifier(name), prev_decl);
  }
  // Structure field declaration
  clang::FieldDecl *CreateFieldDecl(clang::RecordDecl *record,
                                    clang::QualType type,
                                    clang::IdentifierInfo *id);

  clang::FieldDecl *CreateFieldDecl(clang::RecordDecl *record,
                                    clang::QualType type, std::string name) {
    return CreateFieldDecl(record, type, CreateIdentifier(name));
  }
  clang::FieldDecl *CreateFieldDecl(clang::RecordDecl *record,
                                    clang::QualType type,
                                    clang::IdentifierInfo *id,
                                    unsigned bitwidth);

  clang::FieldDecl *CreateFieldDecl(clang::RecordDecl *record,
                                    clang::QualType type, std::string name,
                                    unsigned bitwidth) {
    return CreateFieldDecl(record, type, CreateIdentifier(name), bitwidth);
  }
  // Enum constant declaration
  clang::EnumConstantDecl *CreateEnumConstantDecl(
      clang::EnumDecl *e, clang::IdentifierInfo *id, clang::Expr *expr,
      clang::EnumConstantDecl *previousConstant = nullptr);

  clang::EnumConstantDecl *CreateEnumConstantDecl(
      clang::EnumDecl *e, std::string name, clang::Expr *expr,
      clang::EnumConstantDecl *previousConstant = nullptr) {
    return CreateEnumConstantDecl(e, CreateIdentifier(name), expr,
                                  previousConstant);
  }
  // Declaration statement
  clang::DeclStmt *CreateDeclStmt(clang::Decl *decl);
  // Declaration reference
  clang::DeclRefExpr *CreateDeclRef(clang::ValueDecl *val);
  // Parentheses
  clang::ParenExpr *CreateParen(clang::Expr *expr);
  // C-style casting
  clang::CStyleCastExpr *CreateCStyleCast(clang::QualType type,
                                          clang::Expr *expr);
  // Unary operators
  clang::UnaryOperator *CreateUnaryOp(clang::UnaryOperatorKind opc,
                                      clang::Expr *expr);

  clang::UnaryOperator *CreateDeref(clang::Expr *expr) {
    return CreateUnaryOp(clang::UO_Deref, expr);
  }

  clang::UnaryOperator *CreateAddrOf(clang::Expr *expr) {
    return CreateUnaryOp(clang::UO_AddrOf, expr);
  }

  clang::UnaryOperator *CreateLNot(clang::Expr *expr) {
    return CreateUnaryOp(clang::UO_LNot, expr);
  }

  clang::UnaryOperator *CreateNot(clang::Expr *expr) {
    return CreateUnaryOp(clang::UO_Not, expr);
  }
  // Binary operators
  clang::BinaryOperator *CreateBinaryOp(clang::BinaryOperatorKind opc,
                                        clang::Expr *lhs, clang::Expr *rhs);
  // Logical binary operators
  clang::BinaryOperator *CreateLAnd(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_LAnd, lhs, rhs);
  }

  clang::BinaryOperator *CreateLOr(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_LOr, lhs, rhs);
  }
  // Comparison operators
  clang::BinaryOperator *CreateEQ(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_EQ, lhs, rhs);
  }

  clang::BinaryOperator *CreateNE(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_NE, lhs, rhs);
  }

  clang::BinaryOperator *CreateGE(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_GE, lhs, rhs);
  }

  clang::BinaryOperator *CreateGT(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_GT, lhs, rhs);
  }

  clang::BinaryOperator *CreateLE(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_LE, lhs, rhs);
  }

  clang::BinaryOperator *CreateLT(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_LT, lhs, rhs);
  }
  // Bitwise binary operators
  clang::BinaryOperator *CreateAnd(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_And, lhs, rhs);
  }

  clang::BinaryOperator *CreateOr(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_Or, lhs, rhs);
  }

  clang::BinaryOperator *CreateXor(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_Xor, lhs, rhs);
  }

  clang::BinaryOperator *CreateShl(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_Shl, lhs, rhs);
  }

  clang::BinaryOperator *CreateShr(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_Shr, lhs, rhs);
  }
  // Arithmetic operators
  clang::BinaryOperator *CreateAdd(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_Add, lhs, rhs);
  }

  clang::BinaryOperator *CreateSub(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_Sub, lhs, rhs);
  }

  clang::BinaryOperator *CreateMul(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_Mul, lhs, rhs);
  }

  clang::BinaryOperator *CreateDiv(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_Div, lhs, rhs);
  }

  clang::BinaryOperator *CreateRem(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_Rem, lhs, rhs);
  }

  clang::BinaryOperator *CreateAssign(clang::Expr *lhs, clang::Expr *rhs) {
    return CreateBinaryOp(clang::BO_Assign, lhs, rhs);
  }
  // Ternary conditional operator
  clang::ConditionalOperator *CreateConditional(clang::Expr *cond,
                                                clang::Expr *lhs,
                                                clang::Expr *rhs);
  // Array access
  clang::ArraySubscriptExpr *CreateArraySub(clang::Expr *base,
                                            clang::Expr *idx);
  // Calls
  clang::CallExpr *CreateCall(clang::Expr *callee,
                              std::vector<clang::Expr *> &args);

  clang::CallExpr *CreateCall(clang::FunctionDecl *func,
                              std::vector<clang::Expr *> &args) {
    return CreateCall(CreateDeclRef(func), args);
  }
  // Intrinsic call
  clang::Expr *CreateBuiltinCall(clang::Builtin::ID builtin,
                                 std::vector<clang::Expr *> &args);
  // Structure field access
  clang::MemberExpr *CreateFieldAcc(clang::Expr *base, clang::FieldDecl *field,
                                    bool is_arrow);

  clang::MemberExpr *CreateDot(clang::Expr *base, clang::FieldDecl *field) {
    return CreateFieldAcc(base, field, /*is_arrow=*/false);
  }

  clang::MemberExpr *CreateArrow(clang::Expr *base, clang::FieldDecl *field) {
    return CreateFieldAcc(base, field, /*is_arrow=*/true);
  }
  // Initializer list
  clang::InitListExpr *CreateInitList(std::vector<clang::Expr *> &exprs);
  // Compound literal
  clang::CompoundLiteralExpr *CreateCompoundLit(clang::QualType type,
                                                clang::Expr *expr);
  // Compound statement
  clang::CompoundStmt *CreateCompoundStmt(std::vector<clang::Stmt *> &stmts);
  // If statement
  clang::IfStmt *CreateIf(clang::Expr *cond, clang::Stmt *then_val,
                          clang::Stmt *else_val = nullptr);
  // While loop
  clang::WhileStmt *CreateWhile(clang::Expr *cond, clang::Stmt *body);
  // Do-while loop
  clang::DoStmt *CreateDo(clang::Expr *cond, clang::Stmt *body);
  // Break
  clang::BreakStmt *CreateBreak();
  // Return
  clang::ReturnStmt *CreateReturn(clang::Expr *retval = nullptr);
  // Typedef declaration
  clang::TypedefDecl *CreateTypedefDecl(clang::DeclContext *decl_ctx,
                                        clang::IdentifierInfo *id,
                                        clang::QualType type);

  clang::TypedefDecl *CreateTypedefDecl(clang::DeclContext *decl_ctx,
                                        std::string name,
                                        clang::QualType type) {
    return CreateTypedefDecl(decl_ctx, CreateIdentifier(name), type);
  }
  // Null statement
  clang::NullStmt *CreateNullStmt();

  clang::SwitchStmt *CreateSwitchStmt(clang::Expr *cond);
  clang::CaseStmt *CreateCaseStmt(clang::Expr *cond);
  clang::DefaultStmt *CreateDefaultStmt(clang::Stmt *body);
};

}  // namespace rellic