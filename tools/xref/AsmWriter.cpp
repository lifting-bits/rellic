/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include <llvm/IR/AssemblyAnnotationWriter.h>
#include <llvm/Support/FormattedStream.h>

#include "Printer.h"
#include "rellic/BC/Util.h"

template <typename TMap, typename TKey>
static void PrintProvenances(llvm::raw_ostream& OS, TKey key,
                             const TMap& provenances) {
  std::vector<unsigned long long> provs{};
  auto range{provenances.equal_range(key)};
  for (auto it{range.first}; it != range.second && it != provenances.end();
       it++) {
    provs.emplace_back((unsigned long long)it->second);
  }
  OS << " data-addr=\"";
  OS.write_hex((unsigned long long)key);
  OS << "\" data-provenance=\"";
  if (provs.size() > 0) {
    for (auto i{0U}; i < provs.size() - 1; ++i) {
      OS.write_hex(provs[i]);
      OS << ',';
    }
    OS.write_hex(provs.back());
  }
  OS << '"';
}

class AAW : public llvm::AssemblyAnnotationWriter {
  const rellic::DecompilationResult::IRToDeclMap& DeclProvenance;
  const rellic::DecompilationResult::IRToStmtMap& StmtProvenance;

 public:
  AAW(const rellic::DecompilationResult::IRToDeclMap& DeclProvenance,
      const rellic::DecompilationResult::IRToStmtMap& StmtProvenance)
      : DeclProvenance(DeclProvenance), StmtProvenance(StmtProvenance) {}

  void emitFunctionAnnot(const llvm::Function* F,
                         llvm::formatted_raw_ostream& OS) override {
    OS << "</span><span class=\"llvm\"";
    PrintProvenances(OS, F, DeclProvenance);
    OS << '>';
  }
  void emitInstructionAnnot(const llvm::Instruction* I,
                            llvm::formatted_raw_ostream& OS) override {
    OS << "</span><span class=\"llvm\"";
    PrintProvenances(OS, I, StmtProvenance);
    OS << '>';
  }
  void emitBasicBlockStartAnnot(const llvm::BasicBlock*,
                                llvm::formatted_raw_ostream& OS) override {
    OS << "</span><span>";
  }
  void emitBasicBlockEndAnnot(const llvm::BasicBlock*,
                              llvm::formatted_raw_ostream& OS) override {
    OS << "</span><span>";
  }
  void printInfoComment(const llvm::Value&,
                        llvm::formatted_raw_ostream& OS) override {
    OS << "</span><span>";
  }
};

void PrintModule(
    const llvm::Module* Module,
    const rellic::DecompilationResult::IRToDeclMap& DeclProvenance,
    const rellic::DecompilationResult::IRToStmtMap& StmtProvenance,
    const rellic::DecompilationResult::IRToTypeDeclMap& TypeProvenance,
    llvm::raw_ostream& OS) {
  for (auto type : Module->getIdentifiedStructTypes()) {
    OS << "<span class=\"llvm\"";
    PrintProvenances(OS, (llvm::Type*)type, TypeProvenance);
    OS << '>' << rellic::LLVMThingToString((llvm::Type*)type) << "</span>\n";
  }

  for (auto& global : Module->globals()) {
    OS << "<span class=\"llvm\"";
    PrintProvenances(OS, &global, DeclProvenance);
    OS << '>' << rellic::LLVMThingToString((llvm::Value*)&global)
       << "</span>\n";
  }

  OS << "<span>";
  AAW aaw(DeclProvenance, StmtProvenance);
  for (auto& function : Module->functions()) {
    function.print(OS, &aaw);
  }
  OS << "</span>";
}