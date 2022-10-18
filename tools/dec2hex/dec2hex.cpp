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
#include <llvm/ADT/APInt.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <rellic/Dec2Hex.h>

#include <iostream>
#include <sstream>
#include <system_error>

DEFINE_string(input, "-", "Input C file.");
DEFINE_string(output, "", "Output C file.");

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

  auto heuristic = [](const llvm::APInt &value) {
    return value.getZExtValue() >= 16;
  };

  if (FLAGS_output.empty()) {
    rellic::ConvertIntegerLiteralsToHex(ast_ctx, llvm::outs(), heuristic);
  } else {
    std::error_code ec;
    llvm::raw_fd_ostream os(FLAGS_output, ec);
    if (ec) {
      LOG(FATAL) << ec.message();
    }
    rellic::ConvertIntegerLiteralsToHex(ast_ctx, os, heuristic);
  }

  google::ShutDownCommandLineFlags();
  google::ShutdownGoogleLogging();

  return EXIT_SUCCESS;
}
