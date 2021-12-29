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
#include <llvm/Support/JSON.h>
#include <llvm/Support/raw_ostream.h>

#include <iostream>
#include <system_error>

#include "rellic/AST/ASTPrinter.h"
#include "rellic/BC/Util.h"
#include "rellic/Decompiler.h"
#include "rellic/Version.h"

#ifndef LLVM_VERSION_STRING
#define LLVM_VERSION_STRING LLVM_VERSION_MAJOR << "." << LLVM_VERSION_MINOR
#endif

DEFINE_string(input, "", "Input LLVM bitcode file.");
DEFINE_string(output, "", "Output file.");
DEFINE_bool(emit_json, false, "Emit JSON token file.");
DEFINE_bool(disable_z3, false, "Disable Z3 based AST tranformations.");
DEFINE_bool(remove_phi_nodes, false,
            "Remove PHINodes from input bitcode before decompilation.");
DEFINE_bool(lower_switch, false,
            "Remove SwitchInst by lowering them to branches.");

DECLARE_bool(version);

namespace {
static llvm::Optional<llvm::APInt> GetPCMetadata(llvm::Value* value) {
  auto inst{llvm::dyn_cast<llvm::Instruction>(value)};
  if (!inst) {
    return llvm::Optional<llvm::APInt>();
  }

  auto pc{inst->getMetadata("pc")};
  if (!pc) {
    return llvm::Optional<llvm::APInt>();
  }

  auto& cop{pc->getOperand(0U)};
  auto cval{llvm::cast<llvm::ConstantAsMetadata>(cop)->getValue()};
  return llvm::cast<llvm::ConstantInt>(cval)->getValue();
}

using TokenList = std::list<rellic::Token>;

static void PrintJSON(llvm::raw_ostream& os, TokenList& tokens,
                      rellic::StmtToIRMap& provenance) {
  llvm::json::Array json_out;
  for (auto tok : tokens) {
    llvm::json::Object json_tok;
    switch (tok.GetKind()) {
      case rellic::TokenKind::Stmt: {
        auto it{provenance.find(tok.GetStmt())};
        if (it != provenance.end()) {
          auto pc{GetPCMetadata(it->second)};
          if (pc.hasValue()) {
            llvm::SmallString<64U> str("");
            pc->toStringUnsigned(str);
            json_tok["address"] = str;
          }
        }
        json_tok["text"] = tok.GetString();
      } break;

      case rellic::TokenKind::Decl:
      case rellic::TokenKind::Type:
      case rellic::TokenKind::Misc:
        json_tok["text"] = tok.GetString();
        break;

      case rellic::TokenKind::Space:
        json_tok["text"] = " ";
        break;

      case rellic::TokenKind::Newline:
        json_tok["text"] = "\n";
        break;

      case rellic::TokenKind::Indent:
        json_tok["text"] = "    ";
        break;

      default:
        LOG(FATAL) << "Unknown token type!";
        break;
    }
    json_out.push_back(std::move(json_tok));
  }

  os << llvm::json::Value(std::move(json_out)) << "\n";
}

static void PrintC(llvm::raw_ostream& os, TokenList& tokens) {
  for (auto tok : tokens) {
    switch (tok.GetKind()) {
      case rellic::TokenKind::Stmt:
      case rellic::TokenKind::Decl:
      case rellic::TokenKind::Type:
      case rellic::TokenKind::Misc:
        os << tok.GetString();
        break;

      case rellic::TokenKind::Space:
        os << ' ';
        break;

      case rellic::TokenKind::Newline:
        os << '\n';
        break;

      case rellic::TokenKind::Indent:
        os << "    ";
        break;

      default:
        LOG(FATAL) << "Unknown token type!";
        break;
    }
  }
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
    std::list<rellic::Token> tokens;
    auto value{result.TakeValue()};
    rellic::DeclTokenizer(tokens, *value.ast)
        .Visit(value.ast->getASTContext().getTranslationUnitDecl());

    if (FLAGS_emit_json) {
      PrintJSON(output, tokens, value.stmt_provenance_map);
    }

    PrintC(output, tokens);
  } else {
    LOG(FATAL) << result.TakeError().message;
  }

  google::ShutDownCommandLineFlags();
  google::ShutdownGoogleLogging();

  return EXIT_SUCCESS;
}
