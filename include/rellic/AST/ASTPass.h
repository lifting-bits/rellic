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

#include <atomic>
#include <memory>

#include "rellic/AST/ASTBuilder.h"
#include "rellic/AST/EGraph.h"
#include "rellic/AST/Util.h"

namespace rellic {

class ASTPass {
  std::atomic_bool stop{false};

 protected:
  EGraph& eg;
  virtual void RunImpl(ClassMap& classes, EClass eclass) = 0;
  virtual void StopImpl() {}

 public:
  ASTPass(EGraph& eg) : eg(eg) {}
  virtual ~ASTPass() = default;
  void Stop() {
    stop = true;
    StopImpl();
  }

  void Run(ClassMap& classes, EClass eclass) {
    stop = false;
    RunImpl(classes, eclass);
  }

  bool Stopped() { return stop; }
};
}  // namespace rellic