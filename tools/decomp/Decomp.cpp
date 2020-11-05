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

#include <clang/Basic/TargetInfo.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/InitializePasses.h>
#include <llvm/Support/raw_ostream.h>

#include <memory>
#include <system_error>

#include "rellic/AST/CondBasedRefine.h"
#include "rellic/AST/DeadStmtElim.h"
#include "rellic/AST/ExprCombine.h"
#include "rellic/AST/GenerateAST.h"
#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/LoopRefine.h"
#include "rellic/AST/NestedCondProp.h"
#include "rellic/AST/NestedScopeCombiner.h"
#include "rellic/AST/ReachBasedRefine.h"
#include "rellic/AST/Z3CondSimplify.h"
#include "rellic/BC/Util.h"
#include "rellic/Version/Version.h"

#ifndef LLVM_VERSION_STRING
#define LLVM_VERSION_STRING LLVM_VERSION_MAJOR << "." << LLVM_VERSION_MINOR
#endif

DEFINE_string(input, "", "Input LLVM bitcode file.");
DEFINE_string(output, "", "Output file.");

DECLARE_bool(version);

namespace {

static void InitOptPasses(void) {
  auto& pr = *llvm::PassRegistry::getPassRegistry();
  initializeCore(pr);
  initializeAnalysis(pr);
}

static bool GeneratePseudocode(llvm::Module& module,
                               llvm::raw_ostream& output) {
  InitOptPasses();

  clang::CompilerInstance ins;
  rellic::InitCompilerInstance(ins, module.getTargetTriple());

  auto& ast_ctx = ins.getASTContext();

  rellic::IRToASTVisitor gen(ast_ctx);

  llvm::legacy::PassManager ast;
  ast.add(rellic::createGenerateASTPass(ast_ctx, gen));
  ast.add(rellic::createDeadStmtElimPass(ast_ctx, gen));
  ast.run(module);

  // Simplifier to use during condition-based refinement
  auto cbr_simplifier = new rellic::Z3CondSimplify(ast_ctx, gen);
  cbr_simplifier->SetZ3Simplifier(
      // Simplify boolean structure with AIGs
      z3::tactic(cbr_simplifier->GetZ3Context(), "aig") &
      // Cheap local simplifier
      z3::tactic(cbr_simplifier->GetZ3Context(), "simplify"));

  llvm::legacy::PassManager cbr;
  cbr.add(cbr_simplifier);
  cbr.add(rellic::createNestedCondPropPass(ast_ctx, gen));
  cbr.add(rellic::createNestedScopeCombinerPass(ast_ctx, gen));
  cbr.add(rellic::createCondBasedRefinePass(ast_ctx, gen));
  cbr.add(rellic::createReachBasedRefinePass(ast_ctx, gen));
  while (cbr.run(module))
    ;

  llvm::legacy::PassManager loop;
  loop.add(rellic::createLoopRefinePass(ast_ctx, gen));
  loop.add(rellic::createNestedScopeCombinerPass(ast_ctx, gen));
  while (loop.run(module))
    ;

  // Simplifier to use during final refinement
  auto fin_simplifier = new rellic::Z3CondSimplify(ast_ctx, gen);
  fin_simplifier->SetZ3Simplifier(
      // Simplify boolean structure with AIGs
      z3::tactic(fin_simplifier->GetZ3Context(), "aig") &
      // Propagate bounds over bit-vectors
      z3::tactic(fin_simplifier->GetZ3Context(), "propagate-bv-bounds") &
      // Eliminate conjunctions using De Morgan laws
      z3::tactic(fin_simplifier->GetZ3Context(), "elim-and") &
      // Tseitin transformation
      z3::tactic(fin_simplifier->GetZ3Context(), "tseitin-cnf") &
      // Contextual simplification
      z3::tactic(fin_simplifier->GetZ3Context(), "ctx-simplify"));

  llvm::legacy::PassManager fin;
  fin.add(fin_simplifier);
  fin.add(rellic::createNestedCondPropPass(ast_ctx, gen));
  fin.add(rellic::createNestedScopeCombinerPass(ast_ctx, gen));
  fin.add(rellic::createExprCombinePass(ast_ctx, gen));
  fin.run(module);

  ast_ctx.getTranslationUnitDecl()->print(output);
  // ast_ctx.getTranslationUnitDecl()->dump(output);

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

  GeneratePseudocode(*module, output);

  google::ShutDownCommandLineFlags();
  google::ShutdownGoogleLogging();

  return EXIT_SUCCESS;
}
