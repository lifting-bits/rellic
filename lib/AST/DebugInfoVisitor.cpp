/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#define GOOGLE_STRIP_LOG 1

#include "rellic/AST/DebugInfoVisitor.h"

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <llvm/IR/DebugInfoMetadata.h>

#include "rellic/BC/Util.h"

namespace rellic {

void DebugInfoVisitor::visitDbgDeclareInst(llvm::DbgDeclareInst& inst) {
  auto* var = inst.getVariable();
  auto* loc = inst.getVariableLocation();

  names[loc] = var->getName().str();
  scopes[loc] = var->getScope();
}

void DebugInfoVisitor::visitInstruction(llvm::Instruction& inst) {
  if (auto* loc = inst.getDebugLoc().get()) {
    scopes[&inst] = loc->getScope();
  }
}

}  // namespace rellic
