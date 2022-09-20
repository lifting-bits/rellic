/*
 * Copyright (c) 2022-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once
#include <clang/AST/ASTContext.h>
#include <clang/Frontend/ASTUnit.h>
#include <rellic/AST/ASTBuilder.h>
#include <rellic/AST/Util.h>

#include <atomic>
#include <memory>

namespace rellic {

class ASTPass {
  std::atomic_bool stop{false};

 protected:
  DecompilationContext& dec_ctx;

  bool changed{false};

  virtual void RunImpl() = 0;
  virtual void StopImpl() {}

 public:
  ASTPass(DecompilationContext& dec_ctx)
      : dec_ctx(dec_ctx) {}
  virtual ~ASTPass() = default;
  void Stop() {
    stop = true;
    StopImpl();
  }

  bool Run() {
    changed = false;
    stop = false;
    RunImpl();
    return changed;
  }

  unsigned Fixpoint() {
    unsigned iter_count{0};
    changed = false;
    auto DoIter = [this]() {
      changed = false;
      RunImpl();
      return changed;
    };
    stop = false;
    while (DoIter()) {
      ++iter_count;
    }

    return iter_count;
  }

  bool Stopped() { return stop; }
};

class CompositeASTPass : public ASTPass {
  std::vector<std::unique_ptr<ASTPass>> passes;

 protected:
  void StopImpl() override {
    for (auto& pass : passes) {
      pass->Stop();
    }
  }

  void RunImpl() override {
    for (auto& pass : passes) {
      if (Stopped()) {
        break;
      }
      changed |= pass->Run();
    }
  }

 public:
  CompositeASTPass(DecompilationContext& dec_ctx)
      : ASTPass(dec_ctx) {}
  std::vector<std::unique_ptr<ASTPass>>& GetPasses() { return passes; }
};
}  // namespace rellic