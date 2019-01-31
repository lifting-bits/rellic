/*
 * Copyright (c) 2019 Trail of Bits, Inc.
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

#include <iostream>

#include <clang/Basic/TargetInfo.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Parse/ParseAST.h>

#include "rellic/AST/CXXToCDecl.h"

#ifndef LLVM_VERSION_STRING
#define LLVM_VERSION_STRING LLVM_VERSION_MAJOR << "." << LLVM_VERSION_MINOR
#endif

#ifndef RELLIC_VERSION_STRING
#define RELLIC_VERSION_STRING "unknown"
#endif  // RELLIC_VERSION_STRING

#ifndef RELLIC_BRANCH_NAME
#define RELLIC_BRANCH_NAME "unknown"
#endif  // RELLIC_BRANCH_NAME

DEFINE_string(input, "", "Input LLVM bitcode file.");
DEFINE_string(output, "", "Output file.");

DECLARE_bool(version);

namespace {

class CXXToCDeclConsumer : public clang::ASTConsumer {
 private:
  rellic::CXXToCDeclVisitor visitor;

 public:
  CXXToCDeclConsumer(clang::ASTContext* cxx, clang::ASTContext* c)
      : visitor(cxx, c) {}

  void HandleTranslationUnit(clang::ASTContext& ctx) {
    visitor.TraverseDecl(ctx.getTranslationUnitDecl());
  }
};

static bool InitCompilerInstance(std::string target_triple,
                                 clang::CompilerInstance& ins) {
  ins.createDiagnostics();
  ins.getTargetOpts().Triple = target_triple;
  ins.setTarget(clang::TargetInfo::CreateTargetInfo(
      ins.getDiagnostics(), ins.getInvocation().TargetOpts));
  ins.createFileManager();
  ins.createSourceManager(ins.getFileManager());
  ins.createPreprocessor(clang::TU_Complete);
  ins.createASTContext();

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
      << "Must specify the path to an input C++ header file.";

  LOG_IF(ERROR, FLAGS_output.empty())
      << "Must specify the path to an output C header file.";

  if (FLAGS_input.empty() || FLAGS_output.empty()) {
    std::cerr << google::ProgramUsage();
    return EXIT_FAILURE;
  }

  std::error_code ec;
  llvm::raw_fd_ostream output(FLAGS_output, ec, llvm::sys::fs::F_Text);
  CHECK(!ec) << "Failed to create output file: " << ec.message();
  
  // Create compiler instances
  clang::CompilerInstance cxx_ins;
  clang::CompilerInstance c_ins;
  // Set language dialects
  cxx_ins.getLangOpts().CPlusPlus = 1;
  c_ins.getLangOpts().C99 = 1;
  // Initialize
  auto target = llvm::sys::getDefaultTargetTriple();
  InitCompilerInstance(target, cxx_ins);
  InitCompilerInstance(target, c_ins);
  // Set the input source file
  clang::FrontendInputFile input(FLAGS_input, clang::InputKind(clang::IK_CXX));
  cxx_ins.InitializeSourceManager(input);
  // Tell diagnostics we're about to run
  cxx_ins.getDiagnosticClient().BeginSourceFile(cxx_ins.getLangOpts(),
                                                &cxx_ins.getPreprocessor());
  // Run the consumer containing our visitor
  auto& cxx_ast = cxx_ins.getASTContext();
  auto& c_ast = c_ins.getASTContext();
  CXXToCDeclConsumer consumer(&cxx_ast, &c_ast);
  clang::ParseAST(cxx_ins.getPreprocessor(), &consumer, cxx_ast);
  // Tell diagnostics we're done
  cxx_ins.getDiagnosticClient().EndSourceFile();
  // Print output
  c_ast.getTranslationUnitDecl()->print(output);

  google::ShutDownCommandLineFlags();
  google::ShutdownGoogleLogging();

  return EXIT_SUCCESS;
}