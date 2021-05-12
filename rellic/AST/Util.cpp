/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#define GOOGLE_STRIP_LOG 1

#include "rellic/AST/Util.h"

#include <clang/AST/Stmt.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

namespace rellic {

bool ReplaceChildren(clang::Stmt *stmt, StmtMap &repl_map) {
  auto change = false;
  for (auto c_it = stmt->child_begin(); c_it != stmt->child_end(); ++c_it) {
    auto s_it = repl_map.find(*c_it);
    if (s_it != repl_map.end()) {
      *c_it = s_it->second;
      change = true;
    }
  }
  return change;
}

}  // namespace rellic
