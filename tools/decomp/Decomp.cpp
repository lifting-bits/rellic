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

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <memory>
#include <system_error>

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/raw_ostream.h>

#include <clang/Basic/TargetInfo.h>

#include "rellic/AST/CondBasedRefine.h"
#include "rellic/AST/DeadStmtElim.h"
#include "rellic/AST/GenerateAST.h"
#include "rellic/AST/IRToASTVisitor.h"
#include "rellic/AST/LoopRefine.h"
#include "rellic/AST/NestedCondProp.h"
#include "rellic/AST/NestedScopeCombiner.h"
#include "rellic/AST/Z3CondSimplify.h"

#include "rellic/BC/Util.h"

#ifndef LLVM_VERSION_STRING
#define LLVM_VERSION_STRING LLVM_VERSION_MAJOR << "." << LLVM_VERSION_MINOR
#endif

#ifndef RELLIC_VERSION_STRING
#define RELLIC_VERSION_STRING "unknown"
#endif  // MCSEMA_VERSION_STRING

#ifndef RELLIC_BRANCH_NAME
#define RELLIC_BRANCH_NAME "unknown"
#endif  // MCSEMA_BRANCH_NAME

DEFINE_string(input, "", "Input LLVM bitcode file.");
DEFINE_string(output, "", "Output file.");

DECLARE_bool(version);

namespace {

static void InitOptPasses(void) {
  auto& pr = *llvm::PassRegistry::getPassRegistry();
  initializeCore(pr);
  initializeVectorization(pr);
  initializeIPO(pr);
  initializeAnalysis(pr);
  initializeTransformUtils(pr);
  initializeInstCombine(pr);
  initializeScalarOpts(pr);
}

static bool InitCompilerInstance(llvm::Module& module,
                                 clang::CompilerInstance& ins) {
  auto inv = std::make_shared<clang::CompilerInvocation>();

  const char* tmp[] = {""};
  ins.setDiagnostics(ins.createDiagnostics(new clang::DiagnosticOptions).get());
  clang::CompilerInvocation::CreateFromArgs(*inv, tmp, tmp,
                                            ins.getDiagnostics());

  inv->getTargetOpts().Triple = module.getTargetTriple();
  ins.setInvocation(inv);
  ins.setTarget(clang::TargetInfo::CreateTargetInfo(
      ins.getDiagnostics(), ins.getInvocation().TargetOpts));

  ins.createFileManager();
  ins.createSourceManager(ins.getFileManager());
  ins.createPreprocessor(clang::TU_Complete);
  ins.createASTContext();

  return true;
}

static bool GeneratePseudocode(llvm::Module& module,
                               llvm::raw_ostream& output) {
  InitOptPasses();

  clang::CompilerInstance ins;
  InitCompilerInstance(module, ins);

  rellic::IRToASTVisitor gen(ins);

  llvm::legacy::PassManager ast;
  ast.add(rellic::createGenerateASTPass(ins, gen));
  ast.add(rellic::createDeadStmtElimPass(ins, gen));
  ast.run(module);

  // Simplifier to use during condition-based refinement
  auto cbr_simplifier = new rellic::Z3CondSimplify(ins, gen);
  cbr_simplifier->SetZ3Simplifier(
      // Simplify boolean structure with AIGs
      z3::tactic(cbr_simplifier->GetZ3Context(), "aig") &
      // Cheap local simplifier
      z3::tactic(cbr_simplifier->GetZ3Context(), "simplify"));

  llvm::legacy::PassManager cbr;
  cbr.add(cbr_simplifier);
  cbr.add(rellic::createNestedCondPropPass(ins, gen));
  cbr.add(rellic::createNestedScopeCombinerPass(ins, gen));
  cbr.add(rellic::createCondBasedRefinePass(ins, gen));
  while (cbr.run(module))
    ;

  llvm::legacy::PassManager loop;
  loop.add(rellic::createLoopRefinePass(ins, gen));
  loop.add(rellic::createNestedScopeCombinerPass(ins, gen));
  while (loop.run(module))
    ;

  // Simplifier to use during final refinement
  auto fin_simplifier = new rellic::Z3CondSimplify(ins, gen);
  fin_simplifier->SetZ3Simplifier(
      // Simplify boolean structure with AIGs
      z3::tactic(fin_simplifier->GetZ3Context(), "aig") &
      // Propagate bounds over bit-vectors
      z3::tactic(fin_simplifier->GetZ3Context(), "propagate-bv-bounds") &
      // Tseitin transformation
      z3::tactic(fin_simplifier->GetZ3Context(), "tseitin-cnf") &
      // Contextual simplification
      z3::tactic(fin_simplifier->GetZ3Context(), "ctx-simplify"));

  llvm::legacy::PassManager fin;
  fin.add(fin_simplifier);
  fin.add(rellic::createNestedCondPropPass(ins, gen));
  fin.add(rellic::createNestedScopeCombinerPass(ins, gen));
  fin.add(rellic::createDeadStmtElimPass(ins, gen));
  fin.run(module);

  ins.getASTContext().getTranslationUnitDecl()->print(output);

  return true;
}
}  // namespace

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

  std::stringstream version;
  version << RELLIC_VERSION_STRING << std::endl
          << "Built from branch: " << RELLIC_BRANCH_NAME << std::endl
          << "Using LLVM " << LLVM_VERSION_STRING << std::endl;

  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();
  google::SetUsageMessage(usage.str());
  google::SetVersionString(version.str());
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