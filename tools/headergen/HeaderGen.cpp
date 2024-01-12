/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */
#include <clang/AST/ASTContext.h>
#include <clang/Tooling/Tooling.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/raw_ostream.h>

#include <iostream>

#include "rellic/AST/DebugInfoCollector.h"
#include "rellic/AST/StructGenerator.h"
#include "rellic/AST/SubprogramGenerator.h"
#include "rellic/BC/Util.h"
#include "rellic/Version.h"

#ifndef LLVM_VERSION_STRING
#define LLVM_VERSION_STRING LLVM_VERSION_MAJOR << "." << LLVM_VERSION_MINOR
#endif

DEFINE_string(input, "", "Input file.");
DEFINE_string(output, "", "Output file.");
DEFINE_bool(generate_prototypes, true, "Generate function prototypes.");

DECLARE_bool(version);

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
        << "    --input INPUT_FILE \\" << std::endl
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
      << "Must specify the path to an input file.";

  LOG_IF(ERROR, FLAGS_output.empty())
      << "Must specify the path to an output file.";

  if (FLAGS_input.empty() || FLAGS_output.empty()) {
    std::cerr << google::ProgramUsage();
    return EXIT_FAILURE;
  }

  auto llvm_ctx{std::make_unique<llvm::LLVMContext>()};
  auto module{rellic::LoadModuleFromFile(llvm_ctx.get(), FLAGS_input)};
  auto dic{std::make_unique<rellic::DebugInfoCollector>()};
  dic->visit(module);
  std::vector<std::string> args{"-Wno-pointer-to-int-cast", "-Wno-pointer-sign",
                                "-target", module->getTargetTriple()};
  auto ast_unit{clang::tooling::buildASTFromCodeWithArgs("", args, "out.c")};
  rellic::StructGenerator strctgen(
      *ast_unit, ast_unit->getASTContext().getTranslationUnitDecl());
  rellic::SubprogramGenerator subgen(*ast_unit, strctgen);
  auto types{dic->GetTypes()};
  strctgen.GenerateDecls(types.begin(), types.end());

  if (FLAGS_generate_prototypes) {
    for (auto func : dic->GetSubprograms()) {
      subgen.VisitSubprogram(func);
    }
  }

  std::error_code ec;
  // FIXME(surovic): Figure out if the fix below works.
  // llvm::raw_fd_ostream output(FLAGS_output, ec, llvm::sys::fs::F_Text);
  llvm::raw_fd_ostream output(FLAGS_output, ec);
  ast_unit->getASTContext().getTranslationUnitDecl()->print(output);
  CHECK(!ec) << "Failed to create output file: " << ec.message();

  google::ShutDownCommandLineFlags();
  google::ShutdownGoogleLogging();

  return EXIT_SUCCESS;
}
