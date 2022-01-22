/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/AST/Decl.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/Type.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include "rellic/Decompiler.h"

void PrintDecl(
    clang::Decl* Decl,
    const rellic::DecompilationResult::DeclToIRMap& DeclProvenance,
    const rellic::DecompilationResult::StmtToIRMap& StmtProvenance,
    const rellic::DecompilationResult::TypeDeclToIRMap& TypeProvenance,
    const clang::PrintingPolicy& Policy, int Indentation,
    llvm::raw_ostream& Out);
void PrintDeclGroup(
    clang::Decl** Begin,
    const rellic::DecompilationResult::DeclToIRMap& DeclProvenance,
    const rellic::DecompilationResult::StmtToIRMap& StmtProvenance,
    const rellic::DecompilationResult::TypeDeclToIRMap& TypeProvenance,
    unsigned NumDecls, llvm::raw_ostream& Out,
    const clang::PrintingPolicy& Policy, unsigned Indentation);
void PrintStmt(
    clang::Stmt* Stmt,
    const rellic::DecompilationResult::DeclToIRMap& DeclProvenance,
    const rellic::DecompilationResult::StmtToIRMap& StmtProvenance,
    const rellic::DecompilationResult::TypeDeclToIRMap& TypeProvenance,
    llvm::raw_ostream& Out, const clang::PrintingPolicy& Policy,
    int Indentation = 0, const clang::ASTContext* Context = nullptr,
    clang::PrinterHelper* Helper = nullptr);
void PrintModule(
    const llvm::Module* Module,
    const rellic::DecompilationResult::IRToDeclMap& DeclProvenance,
    const rellic::DecompilationResult::IRToStmtMap& StmtProvenance,
    const rellic::DecompilationResult::IRToTypeDeclMap& TypeProvenance,
    llvm::raw_ostream& ROS, llvm::AssemblyAnnotationWriter* AAW = nullptr,
    bool ShouldPreserveUseListOrder = false, bool IsForDebug = false);
void PrintType(
    clang::QualType Type,
    const rellic::DecompilationResult::DeclToIRMap& DeclProvenance,
    const rellic::DecompilationResult::StmtToIRMap& StmtProvenance,
    const rellic::DecompilationResult::TypeDeclToIRMap& TypeProvenance,
    llvm::raw_ostream& Out, const clang::PrintingPolicy& Policy,
    const llvm::Twine& PlaceHolder = llvm::Twine(), unsigned Indentation = 0);
std::string GetQualTypeAsString(
    clang::QualType Type, const clang::PrintingPolicy& Policy,
    const rellic::DecompilationResult::DeclToIRMap& DeclProvenance,
    const rellic::DecompilationResult::StmtToIRMap& StmtProvenance,
    const rellic::DecompilationResult::TypeDeclToIRMap& TypeProvenance);
std::string GetTypeAsString(
    const clang::Type* Ty, const clang::PrintingPolicy& Policy,
    const rellic::DecompilationResult::DeclToIRMap& DeclProvenance,
    const rellic::DecompilationResult::StmtToIRMap& StmtProvenance,
    const rellic::DecompilationResult::TypeDeclToIRMap& TypeProvenance);
void PrintQualifiers(const clang::Qualifiers& Qualifiers, llvm::raw_ostream& OS,
                     const clang::PrintingPolicy& Policy,
                     bool appendSpaceIfNonEmpty = false);
std::string GetQualifiersAsString(const clang::Qualifiers& Qualifiers);
std::string GetQualifiersAsString(const clang::Qualifiers& Qualifiers,
                                  const clang::PrintingPolicy& Policy);