/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include <clang/Basic/TargetInfo.h>
#include <clang/Tooling/Tooling.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/InitializePasses.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/Transforms/Utils/Local.h>

#include <memory>
#include <sstream>
#include <system_error>

#include "rellic/AST/CondBasedRefine.h"
#include "rellic/AST/DeadStmtElim.h"
#include "rellic/AST/DebugInfoVisitor.h"
#include "rellic/AST/ExprCombine.h"
#include "rellic/AST/GenerateAST.h"
#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/LoopRefine.h"
#include "rellic/AST/NestedCondProp.h"
#include "rellic/AST/NestedScopeCombine.h"
#include "rellic/AST/ReachBasedRefine.h"
#include "rellic/AST/Z3CondSimplify.h"
#include "rellic/BC/Util.h"
#include "rellic/Version/Version.h"

#ifndef LLVM_VERSION_STRING
#define LLVM_VERSION_STRING LLVM_VERSION_MAJOR << "." << LLVM_VERSION_MINOR
#endif

DEFINE_string(input, "", "Input LLVM bitcode file.");
DEFINE_string(output, "", "Output file.");
DEFINE_bool(disable_z3, false, "Disable Z3 based AST tranformations.");
DEFINE_bool(remove_phi_nodes, false,
            "Remove PHINodes from input bitcode before decompilation.");
DEFINE_bool(lower_switch, false,
            "Remove SwitchInst by lowering them to branches.");

DECLARE_bool(version);

namespace {

static void RemovePHINodes(llvm::Module& module) {
  std::vector<llvm::PHINode*> work_list;
  for (auto& func : module) {
    for (auto& inst : llvm::instructions(func)) {
      if (auto phi = llvm::dyn_cast<llvm::PHINode>(&inst)) {
        work_list.push_back(phi);
      }
    }
  }
  for (auto phi : work_list) {
    DemotePHIToStack(phi);
  }
}

static void LowerSwitches(llvm::Module& module) {
  llvm::legacy::PassManager pm;
  pm.add(llvm::createLowerSwitchPass());
  pm.run(module);
}

static void InitOptPasses(void) {
  auto& pr = *llvm::PassRegistry::getPassRegistry();
  initializeCore(pr);
  initializeAnalysis(pr);
}

using StmtToIRMap = std::unordered_map<clang::Stmt*, llvm::Value*>;

static void InitProvenanceMap(StmtToIRMap& provenance,
                              rellic::IRToStmtMap& init) {
  for (auto& item : init) {
    if (item.second) {
      provenance[item.second] = item.first;
    }
  }
}

static void UpdateProvenanceMap(StmtToIRMap& provenance,
                                rellic::StmtSubMap& substitutions) {
  for (auto& sub : substitutions) {
    auto it{provenance.find(sub.first)};
    if (it != provenance.end()) {
      provenance[sub.second] = it->second;
      provenance.erase(it);
    }
  }
}

static bool GeneratePseudocode(llvm::Module& module,
                               llvm::raw_ostream& output) {
  InitOptPasses();
  rellic::DebugInfoVisitor visitor;
  visitor.visit(module);

  std::vector<std::string> args{"-Wno-pointer-to-int-cast", "-target",
                                module.getTargetTriple()};
  auto ast_unit{clang::tooling::buildASTFromCodeWithArgs("", args, "out.c")};

  llvm::legacy::PassManager pm_ast;
  rellic::GenerateAST* gr{new rellic::GenerateAST(*ast_unit, visitor.GetIRToNameMap())};
  rellic::DeadStmtElim* dse{new rellic::DeadStmtElim(*ast_unit)};
  pm_ast.add(gr);
  pm_ast.add(dse);
  pm_ast.run(module);

  StmtToIRMap stmt_provenance;

  InitProvenanceMap(stmt_provenance, gr->GetIRToStmtMap());
  UpdateProvenanceMap(stmt_provenance, dse->GetStmtSubMap());

  rellic::Z3CondSimplify* zcs{new rellic::Z3CondSimplify(*ast_unit)};
  rellic::NestedCondProp* ncp{new rellic::NestedCondProp(*ast_unit)};
  rellic::NestedScopeCombine* nsc{new rellic::NestedScopeCombine(*ast_unit)};
  rellic::CondBasedRefine* cbr{new rellic::CondBasedRefine(*ast_unit)};
  rellic::ReachBasedRefine* rbr{new rellic::ReachBasedRefine(*ast_unit)};

  llvm::legacy::PassManager pm_cbr;
  if (!FLAGS_disable_z3) {
    // Simplifier to use during condition-based refinement
    zcs->SetZ3Simplifier(
        // Simplify boolean structure with AIGs
        z3::tactic(zcs->GetZ3Context(), "aig") &
        // Cheap local simplifier
        z3::tactic(zcs->GetZ3Context(), "simplify"));
    pm_cbr.add(zcs);
    pm_cbr.add(ncp);
  }

  pm_cbr.add(nsc);

  if (!FLAGS_disable_z3) {
    pm_cbr.add(cbr);
    pm_cbr.add(rbr);
  }

  while (pm_cbr.run(module)) {
    UpdateProvenanceMap(stmt_provenance, zcs->GetStmtSubMap());
    UpdateProvenanceMap(stmt_provenance, ncp->GetStmtSubMap());
    UpdateProvenanceMap(stmt_provenance, nsc->GetStmtSubMap());
    UpdateProvenanceMap(stmt_provenance, cbr->GetStmtSubMap());
    UpdateProvenanceMap(stmt_provenance, rbr->GetStmtSubMap());
  }

  rellic::LoopRefine* lr{new rellic::LoopRefine(*ast_unit)};
  nsc = new rellic::NestedScopeCombine(*ast_unit);

  llvm::legacy::PassManager pm_loop;
  pm_loop.add(lr);
  pm_loop.add(nsc);
  while (pm_loop.run(module)) {
    UpdateProvenanceMap(stmt_provenance, lr->GetStmtSubMap());
    UpdateProvenanceMap(stmt_provenance, nsc->GetStmtSubMap());
  }

  llvm::legacy::PassManager pm_scope;
  if (!FLAGS_disable_z3) {
    // Simplifier to use during final refinement
    zcs = new rellic::Z3CondSimplify(*ast_unit);
    ncp = new rellic::NestedCondProp(*ast_unit);
    zcs->SetZ3Simplifier(
        // Simplify boolean structure with AIGs
        z3::tactic(zcs->GetZ3Context(), "aig") &
        // Cheap simplification
        z3::tactic(zcs->GetZ3Context(), "simplify") &
        // Propagate bounds over bit-vectors
        z3::tactic(zcs->GetZ3Context(), "propagate-bv-bounds") &
        // Contextual simplification
        z3::tactic(zcs->GetZ3Context(), "ctx-simplify"));
    pm_scope.add(zcs);
    pm_scope.add(ncp);
  }

  nsc = new rellic::NestedScopeCombine(*ast_unit);

  pm_scope.add(nsc);
  while (pm_scope.run(module)) {
    UpdateProvenanceMap(stmt_provenance, zcs->GetStmtSubMap());
    UpdateProvenanceMap(stmt_provenance, ncp->GetStmtSubMap());
    UpdateProvenanceMap(stmt_provenance, nsc->GetStmtSubMap());
  }

  llvm::legacy::PassManager pm_expr;
  rellic::ExprCombine* ec{new rellic::ExprCombine(*ast_unit)};
  pm_expr.add(ec);
  while (pm_expr.run(module)) {
    UpdateProvenanceMap(stmt_provenance, ec->GetStmtSubMap());
  }

  ast_unit->getASTContext().getTranslationUnitDecl()->print(output);
  // ast_unit->getASTContext().getTranslationUnitDecl()->dump(output);

  return true;
}
}  // namespace

static void SetVersion(void) {
  std::stringstream version;

  auto vs = rellic::Version::GetVersionString();
  if (0 == vs.size()) {
    vs = "unknown";
  }
  version << vs << "\n";
  if (!rellic::Version::HasVersionData()) {
    version << "No extended version information found!\n";
  } else {
    version << "Commit Hash: " << rellic::Version::GetCommitHash() << "\n";
    version << "Commit Date: " << rellic::Version::GetCommitDate() << "\n";
    version << "Last commit by: " << rellic::Version::GetAuthorName() << " ["
            << rellic::Version::GetAuthorEmail() << "]\n";
    version << "Commit Subject: [" << rellic::Version::GetCommitSubject()
            << "]\n";
    version << "\n";
    if (rellic::Version::HasUncommittedChanges()) {
      version << "Uncommitted changes were present during build.\n";
    } else {
      version << "All changes were committed prior to building.\n";
    }
  }
  version << "Using LLVM " << LLVM_VERSION_STRING << std::endl;

  google::SetVersionString(version.str());
}

int main(int argc, char* argv[]) {
  std::stringstream usage;
  usage << std::endl
        << std::endl
        << "  " << argv[0] << " \\" << std::endl
        << "    --input INPUT_BC_FILE \\" << std::endl
        << "    --output OUTPUT_C_FILE \\" << std::endl
        << std::endl

        // Print the version and exit.
        << "    [--version]" << std::endl
        << std::endl;

  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();
  google::SetUsageMessage(usage.str());
  SetVersion();
  google::ParseCommandLineFlags(&argc, &argv, true);

  LOG_IF(ERROR, FLAGS_input.empty())
      << "Must specify the path to an input LLVM bitcode file.";

  LOG_IF(ERROR, FLAGS_output.empty())
      << "Must specify the path to an output C file.";

  if (FLAGS_input.empty() || FLAGS_output.empty()) {
    std::cerr << google::ProgramUsage();
    return EXIT_FAILURE;
  }

  std::unique_ptr<llvm::LLVMContext> llvm_ctx(new llvm::LLVMContext);

  auto module = rellic::LoadModuleFromFile(llvm_ctx.get(), FLAGS_input);

  std::error_code ec;
  llvm::raw_fd_ostream output(FLAGS_output, ec, llvm::sys::fs::F_Text);
  CHECK(!ec) << "Failed to create output file: " << ec.message();

  if (FLAGS_remove_phi_nodes) {
    RemovePHINodes(*module);
  }

  if (FLAGS_lower_switch) {
    LowerSwitches(*module);
  }

  GeneratePseudocode(*module, output);

  google::ShutDownCommandLineFlags();
  google::ShutdownGoogleLogging();

  return EXIT_SUCCESS;
}
