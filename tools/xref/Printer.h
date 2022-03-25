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

void PrintDecl(clang::Decl* Decl, const clang::PrintingPolicy& Policy,
               int Indentation, llvm::raw_ostream& Out);
void PrintDeclGroup(clang::Decl** Begin, unsigned NumDecls,
                    llvm::raw_ostream& Out, const clang::PrintingPolicy& Policy,
                    unsigned Indentation);
void PrintStmt(clang::Stmt* Stmt, llvm::raw_ostream& Out,
               const clang::PrintingPolicy& Policy, int Indentation = 0,
               const clang::ASTContext* Context = nullptr,
               clang::PrinterHelper* Helper = nullptr);
void PrintType(clang::QualType Type, llvm::raw_ostream& Out,
               const clang::PrintingPolicy& Policy,
               const llvm::Twine& PlaceHolder = llvm::Twine(),
               unsigned Indentation = 0);
std::string GetQualTypeAsString(clang::QualType Type,
                                const clang::PrintingPolicy& Policy);
std::string GetTypeAsString(const clang::Type* Ty,
                            const clang::PrintingPolicy& Policy);
void PrintQualifiers(const clang::Qualifiers& Qualifiers, llvm::raw_ostream& OS,
                     const clang::PrintingPolicy& Policy,
                     bool appendSpaceIfNonEmpty = false);
std::string GetQualifiersAsString(const clang::Qualifiers& Qualifiers);
std::string GetQualifiersAsString(const clang::Qualifiers& Qualifiers,
                                  const clang::PrintingPolicy& Policy);