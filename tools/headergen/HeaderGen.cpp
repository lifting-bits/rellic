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

#include <fstream>
#include <iostream>
#include <streambuf>

#include <clang/Tooling/Tooling.h>

#include "rellic/AST/CXXToCDecl.h"
#include "rellic/AST/Util.h"

#include <rellic/Version/Version.h>

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
  if(0 == vs.size()) {
      vs = "unknown";
  }
  version << vs << "\n";
  if(!rellic::Version::HasVersionData()) {
    version << "No extended version information found!\n";
  } else {
    version << "Commit Hash: " << rellic::Version::GetCommitHash() << "\n";
    version << "Commit Date: " << rellic::Version::GetCommitDate() << "\n";
    version << "Last commit by: " << rellic::Version::GetAuthorName() << " [" << rellic::Version::GetAuthorEmail() << "]\n";
    version << "Commit Subject: [" << rellic::Version::GetCommitSubject() << "]\n";
    version << "\n";
    if(rellic::Version::HasUncommittedChanges()) {
      version << "Uncommitted changes were present during build.\n";
    } else  {
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
  llvm::raw_fd_ostream output(FLAGS_output, ec, llvm::sys::fs::F_Text);
  CHECK(!ec) << "Failed to create output file: " << ec.message();
  // Read a CXX AST from our input file
  auto cxx_ast_unit =
      clang::tooling::buildASTFromCode(ReadFile(FLAGS_input), FLAGS_input);
  // Exit if AST generation has failed
  if (cxx_ast_unit->getDiagnostics().hasErrorOccurred()) {
    return EXIT_FAILURE;
  }
  // Run our visitor on the CXX AST
  clang::CompilerInstance c_ins;
  rellic::InitCompilerInstance(c_ins);
  auto& c_ast_ctx = c_ins.getASTContext();
  rellic::CXXToCDeclVisitor visitor(c_ast_ctx);
  // cxx_ast_unit->getASTContext().getTranslationUnitDecl()->dump();
  visitor.TraverseDecl(cxx_ast_unit->getASTContext().getTranslationUnitDecl());
  // Print output
  c_ast_ctx.getTranslationUnitDecl()->print(output);

  google::ShutDownCommandLineFlags();
  google::ShutdownGoogleLogging();

  return EXIT_SUCCESS;
}
