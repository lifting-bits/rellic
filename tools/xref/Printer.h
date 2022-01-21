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
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include "rellic/Decompiler.h"

void PrintDecl(clang::Decl* Decl,
               const rellic::DecompilationResult::DeclToIRMap& DeclProvenance,
               const rellic::DecompilationResult::StmtToIRMap& StmtProvenance,
               const clang::PrintingPolicy& Policy, int Indentation,
               llvm::raw_ostream& Out);
void PrintStmt(clang::Stmt* Stmt,
               const rellic::DecompilationResult::StmtToIRMap& StmtProvenance,
               llvm::raw_ostream& Out, const clang::PrintingPolicy& Policy,
               int Indentation = 0, const clang::ASTContext* Context = nullptr,
               clang::PrinterHelper* Helper = nullptr);
void PrintModule(const llvm::Module* Module,
                 const rellic::DecompilationResult::IRToDeclMap& DeclProvenance,
                 const rellic::DecompilationResult::IRToStmtMap& StmtProvenance,
                 llvm::raw_ostream& ROS,
                 llvm::AssemblyAnnotationWriter* AAW = nullptr,
                 bool ShouldPreserveUseListOrder = false,
                 bool IsForDebug = false);