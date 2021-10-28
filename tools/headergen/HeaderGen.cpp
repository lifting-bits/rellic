/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include <clang/Tooling/Tooling.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/Support/Host.h>

#include <fstream>
#include <iostream>
#include <streambuf>

#include "rellic/AST/CXXToCDecl.h"
#include "rellic/Version/Version.h"

#ifndef LLVM_VERSION_STRING
#define LLVM_VERSION_STRING LLVM_VERSION_MAJOR << "." << LLVM_VERSION_MINOR
#endif

DEFINE_string(input, "", "Input header file.");
DEFINE_string(output, "", "Output file.");

DECLARE_bool(version);

namespace {

static std::string ReadFile(std::string path) {
  auto err_or_buf = llvm::MemoryBuffer::getFile(path);
  if (!err_or_buf) {
    auto msg = err_or_buf.getError().message();
    LOG(FATAL) << "Failed to read input file: " << msg;
  }
  return err_or_buf.get()->getBuffer().str();
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
        << "    --input INPUT_HEADER_FILE \\" << std::endl
        << "    --output OUTPUT_FILE \\" << std::endl
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
      << "Must specify the path to an input header file.";

  LOG_IF(ERROR, FLAGS_output.empty())
      << "Must specify the path to an output file.";

  if (FLAGS_input.empty() || FLAGS_output.empty()) {
    std::cerr << google::ProgramUsage();
    return EXIT_FAILURE;
  }

  std::error_code ec;
  // FIXME(surovic): Figure out if the fix below works.
  // llvm::raw_fd_ostream output(FLAGS_output, ec, llvm::sys::fs::F_Text);
  llvm::raw_fd_ostream output(FLAGS_output, ec);
  CHECK(!ec) << "Failed to create output file: " << ec.message();
  // Read a CXX AST from our input file
  auto cxx_ast_unit =
      clang::tooling::buildASTFromCode(ReadFile(FLAGS_input), FLAGS_input);
  // Exit if AST generation has failed
  if (cxx_ast_unit->getDiagnostics().hasErrorOccurred()) {
    return EXIT_FAILURE;
  }
  // Run our visitor on the CXX AST
  std::vector<std::string> args{"-target", llvm::sys::getDefaultTargetTriple()};
  auto ast_unit{clang::tooling::buildASTFromCodeWithArgs("", args, "out.c")};
  auto& c_ast_ctx = ast_unit->getASTContext();
  rellic::CXXToCDeclVisitor visitor(*ast_unit);
  // cxx_ast_unit->getASTContext().getTranslationUnitDecl()->dump();
  visitor.TraverseDecl(c_ast_ctx.getTranslationUnitDecl());
  // Print output
  c_ast_ctx.getTranslationUnitDecl()->print(output);

  google::ShutDownCommandLineFlags();
  google::ShutdownGoogleLogging();

  return EXIT_SUCCESS;
}
