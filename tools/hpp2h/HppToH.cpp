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
#include <memory>

#include <clang/AST/ASTConsumer.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendOptions.h>
#include <clang/Parse/ParseAST.h>

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

using namespace clang;

class FindNamedClassVisitor
    : public RecursiveASTVisitor<FindNamedClassVisitor> {
 public:
  explicit FindNamedClassVisitor(ASTContext* Context) {}

  bool VisitCXXRecordDecl(CXXRecordDecl* Declaration) {
    LOG(INFO) << "Yeet!";
    return true;
  }
};

class FindNamedClassConsumer : public clang::ASTConsumer {
 public:
  explicit FindNamedClassConsumer(ASTContext* Context) : Visitor(Context) {}

  virtual void HandleTranslationUnit(clang::ASTContext& Context) {
    Visitor.TraverseDecl(Context.getTranslationUnitDecl());
  }

 private:
  FindNamedClassVisitor Visitor;
};

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
  // Create a compiler
  clang::CompilerInstance ins;
  // Set language to C++
  ins.getLangOpts().CPlusPlus = 1;
  // Initialize the compiler
  ins.createDiagnostics();
  ins.getTargetOpts().Triple = llvm::sys::getDefaultTargetTriple();
  ins.setTarget(clang::TargetInfo::CreateTargetInfo(
      ins.getDiagnostics(), ins.getInvocation().TargetOpts));
  ins.createFileManager();
  ins.createSourceManager(ins.getFileManager());
  ins.createPreprocessor(clang::TU_Complete);
  ins.createASTContext();
  // Set the input source file
  clang::FrontendInputFile input(FLAGS_input, clang::InputKind(clang::IK_CXX));
  ins.InitializeSourceManager(input);
  // Tell diagnostics we're about to run
  ins.getDiagnosticClient().BeginSourceFile(ins.getLangOpts(),
                                            &ins.getPreprocessor());
  // Run the consumer containing our visitor
  auto& ast_ctx = ins.getASTContext();
  FindNamedClassConsumer consumer(&ast_ctx);
  clang::ParseAST(ins.getPreprocessor(), &consumer, ast_ctx);
  // Tell diagnostics we're done
  ins.getDiagnosticClient().EndSourceFile();

  google::ShutDownCommandLineFlags();
  google::ShutdownGoogleLogging();

  return EXIT_SUCCESS;
}