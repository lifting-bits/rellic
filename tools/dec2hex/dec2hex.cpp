/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include <clang/AST/Expr.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Tooling/Tooling.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>

#include <iostream>
#include <sstream>
#include <system_error>

DEFINE_string(input, "-", "Input C file.");
DEFINE_string(output, "", "Output C file.");

namespace {
using namespace clang;
using namespace clang::ast_matchers;

StatementMatcher intlit = integerLiteral().bind("intlit");

class IntegerReplacer : public MatchFinder::MatchCallback {
  Rewriter &rw;

 public:
  IntegerReplacer(Rewriter &rw) : rw(rw) {}
  virtual void run(const MatchFinder::MatchResult &Result) {
    if (auto lit = Result.Nodes.getNodeAs<IntegerLiteral>("intlit")) {
      llvm::SmallString<40> str;
      lit->getValue().toString(
          str, /*radix=*/16, /*isSigned=*/lit->getType()->isSignedIntegerType(),
          /*formatAsCLiteral=*/true);
      std::string res;
      llvm::raw_string_ostream OS(res);
      OS << str;
      switch (lit->getType()->castAs<BuiltinType>()->getKind()) {
        default:
          llvm_unreachable("Unexpected type for integer literal!");
        case BuiltinType::Char_S:
        case BuiltinType::Char_U:
          OS << "i8";
          break;
        case BuiltinType::UChar:
          OS << "Ui8";
          break;
        case BuiltinType::Short:
          OS << "i16";
          break;
        case BuiltinType::UShort:
          OS << "Ui16";
          break;
        case BuiltinType::Int:
          break;  // no suffix.
        case BuiltinType::UInt:
          OS << 'U';
          break;
        case BuiltinType::Long:
          OS << 'L';
          break;
        case BuiltinType::ULong:
          OS << "UL";
          break;
        case BuiltinType::LongLong:
          OS << "LL";
          break;
        case BuiltinType::ULongLong:
          OS << "ULL";
          break;
      }
      rw.ReplaceText(lit->getSourceRange(), res);
    }
  }
};
}  // namespace

int main(int argc, char *argv[]) {
  std::stringstream usage;
  usage << std::endl
        << std::endl
        << "  " << argv[0] << " \\" << std::endl
        << "    --input INPUT_C_FILE \\" << std::endl
        << "    --output OUTPUT_C_FILE \\" << std::endl
        << std::endl;

  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();
  google::SetUsageMessage(usage.str());
  google::ParseCommandLineFlags(&argc, &argv, true);

  auto input_file = llvm::MemoryBuffer::getFileOrSTDIN(FLAGS_input);
  if (!input_file) {
    LOG(FATAL) << input_file.getError().message();
  }
  auto ast_unit{clang::tooling::buildASTFromCodeWithArgs(
      input_file.get()->getBuffer(), {}, FLAGS_input, "rellic-dec2hex")};
  auto &ast_ctx{ast_unit->getASTContext()};

  clang::Rewriter rewriter{ast_ctx.getSourceManager(), ast_ctx.getLangOpts()};
  clang::ast_matchers::MatchFinder finder;
  IntegerReplacer replacer{rewriter};

  finder.addMatcher(intlit, &replacer);
  finder.matchAST(ast_ctx);

  if (FLAGS_output.empty()) {
    rewriter.getRewriteBufferFor(ast_ctx.getSourceManager().getMainFileID())
        ->write(llvm::outs());
  } else {
    std::error_code ec;
    llvm::raw_fd_ostream os(FLAGS_output, ec);
    if (ec) {
      LOG(FATAL) << ec.message();
    }
    rewriter.getRewriteBufferFor(ast_ctx.getSourceManager().getMainFileID())
        ->write(os);
  }

  google::ShutDownCommandLineFlags();
  google::ShutdownGoogleLogging();

  return EXIT_SUCCESS;
}
