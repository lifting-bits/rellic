/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include <clang/Tooling/Tooling.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/IR/AssemblyAnnotationWriter.h>
#include <llvm/Support/raw_ostream.h>

#include <iostream>
#include <system_error>

#include "Printer.h"
#include "rellic/BC/Util.h"
#include "rellic/Decompiler.h"
#include "rellic/Version.h"

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
DEFINE_bool(output_http, false, "Outputs HTTP headers (useful for CGI)");
DEFINE_bool(standalone_html, true,
            "Whether to output a full HTML page or only content");

DECLARE_bool(version);

namespace {
static llvm::Optional<llvm::APInt> GetPCMetadata(llvm::Value *value) {
  auto inst{llvm::dyn_cast<llvm::Instruction>(value)};
  if (!inst) {
    return llvm::Optional<llvm::APInt>();
  }

  auto pc{inst->getMetadata("pc")};
  if (!pc) {
    return llvm::Optional<llvm::APInt>();
  }

  auto &cop{pc->getOperand(0U)};
  auto cval{llvm::cast<llvm::ConstantAsMetadata>(cop)->getValue()};
  return llvm::cast<llvm::ConstantInt>(cval)->getValue();
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

int main(int argc, char *argv[]) {
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
      << "Must specify the path to an output HTML file.";

  if (FLAGS_input.empty() || FLAGS_output.empty()) {
    std::cerr << google::ProgramUsage();
    return EXIT_FAILURE;
  }

  std::unique_ptr<llvm::LLVMContext> llvm_ctx(new llvm::LLVMContext);

  auto module{std::unique_ptr<llvm::Module>(
      rellic::LoadModuleFromFile(llvm_ctx.get(), FLAGS_input))};

  std::error_code ec;
  llvm::raw_fd_ostream output(FLAGS_output, ec);
  CHECK(!ec) << "Failed to create output file: " << ec.message();

  rellic::DecompilationOptions opts{};
  opts.disable_z3 = FLAGS_disable_z3;
  opts.lower_switches = FLAGS_lower_switch;
  opts.remove_phi_nodes = FLAGS_remove_phi_nodes;

  auto result{rellic::Decompile(std::move(module), opts)};
  if (result.Succeeded()) {
    if (FLAGS_output_http) {
      output << "Content-Type: text/html\nStatus: 200\n\n";
    }

    auto value{result.TakeValue()};
    auto &context{value.ast->getASTContext()};
    if (FLAGS_standalone_html) {
    output << R"html(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta http-equiv="X-UA-Compatible" content="IE=edge">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
    .hover {
        background-color: rgba(0, 0, 0, 0.1);
    }

    body {
        display: flex;
        flex-wrap: wrap;
    }

    pre {
        border: 1px solid grey;
        margin: 0.5em;
        padding: 0.5em;
        max-width: 45vw;
        overflow-x: auto;
        height: 95vh;
    }
    </style>
</head>
<body>)html";
    }
    output << "<pre>";
    PrintDecl(context.getTranslationUnitDecl(), value.decl_provenance_map,
              value.stmt_provenance_map, context.getPrintingPolicy(), 0,
              output);
    output << "</pre><pre>";
    PrintModule(value.module.get(), value.value_to_decl_map,
                value.value_to_stmt_map, output);
    output << "</pre>\n";
    output << R"html(<script>
    const spans = document.querySelectorAll('[data-provenance]')

    for (let span of spans) {
        span.addEventListener('mouseover', e => {
            e.stopImmediatePropagation()
            span.classList.add('hover')
            const provenanceAddr = span.dataset.provenance
            const provenanceSpans = document.querySelectorAll(`[data-addr='${provenanceAddr}']`)
            for (let prov of provenanceSpans) {
                prov.classList.add('hover')
            }
        })
        span.addEventListener('mouseleave', e => {
            span.classList.remove('hover')
            const provenanceAddr = span.dataset.provenance
            const provenanceSpans = document.querySelectorAll(`[data-addr='${provenanceAddr}']`)
            for (let prov of provenanceSpans) {
                prov.classList.remove('hover')
            }
        })
    }
</script>
</body>
</html>)html";
  } else {
    if (FLAGS_output_http) {
      output << "Content-Type: text/plain\nStatus: 500\n\n";
    }
    LOG(FATAL) << result.TakeError().message;
  }

  google::ShutDownCommandLineFlags();
  google::ShutdownGoogleLogging();

  return EXIT_SUCCESS;
}
