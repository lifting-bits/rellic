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
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>

namespace rellic {
namespace {
using namespace clang;
using namespace clang::ast_matchers;

StatementMatcher intlit = integerLiteral().bind("intlit");

class IntegerReplacer : public MatchFinder::MatchCallback {
  Rewriter &rw;
  std::function<bool(const llvm::APInt &)> shouldConvert;

 public:
  IntegerReplacer(Rewriter &rw,
                  std::function<bool(const llvm::APInt &)> shouldConvert)
      : rw(rw), shouldConvert(shouldConvert) {}
  virtual void run(const MatchFinder::MatchResult &Result) {
    if (auto lit = Result.Nodes.getNodeAs<IntegerLiteral>("intlit")) {
      if (!shouldConvert(lit->getValue())) {
        return;
      }

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

void ConvertIntegerLiteralsToHex(
    clang::ASTContext &ast_ctx, llvm::raw_ostream &os,
    std::function<bool(const llvm::APInt &)> shouldConvert) {
  auto &sm{ast_ctx.getSourceManager()};
  clang::Rewriter rewriter{sm, ast_ctx.getLangOpts()};
  clang::ast_matchers::MatchFinder finder;
  IntegerReplacer replacer{rewriter, shouldConvert};

  finder.addMatcher(intlit, &replacer);
  finder.matchAST(ast_ctx);
  rewriter.getRewriteBufferFor(sm.getMainFileID())->write(os);
}
}  // namespace rellic
