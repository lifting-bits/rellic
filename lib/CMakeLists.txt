#
# Copyright (c) 2020-present, Trail of Bits, Inc.
# All rights reserved.
#
# This source code is licensed in accordance with the terms specified in
# the LICENSE file found in the root directory of this source tree.
#

# Based on: https://github.com/andrew-hardin/cmake-git-version-tracking/blob/master/better-example/CMakeLists.txt
# By Andrew Hardin
# Released under the MIT License.
# https://raw.githubusercontent.com/andrew-hardin/cmake-git-version-tracking/master/LICENSE
#
# Define the two required variables before including
# the source code for watching a git repository.
set(PRE_CONFIGURE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/Version.cpp.in")
set(POST_CONFIGURE_FILE "${CMAKE_CURRENT_BINARY_DIR}/Version.cpp")
set(GIT_WORKING_DIR "${PROJECT_SOURCE_DIR}")
include("${PROJECT_SOURCE_DIR}/cmake/git_watcher.cmake")

set(include_dir "${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}")

set(AST_HEADERS
  "${include_dir}/AST/ASTBuilder.h"
  "${include_dir}/AST/CXXToCDecl.h"
  "${include_dir}/AST/CondBasedRefine.h"
  "${include_dir}/AST/DeadStmtElim.h"
  "${include_dir}/AST/DebugInfoCollector.h"
  "${include_dir}/AST/ExprCombine.h"
  "${include_dir}/AST/GenerateAST.h"
  "${include_dir}/AST/IRToASTVisitor.h"
  "${include_dir}/AST/InferenceRule.h"
  "${include_dir}/AST/LocalDeclRenamer.h"
  "${include_dir}/AST/LoopRefine.h"
  "${include_dir}/AST/NestedCondProp.h"
  "${include_dir}/AST/NestedScopeCombine.h"
  "${include_dir}/AST/ReachBasedRefine.h"
  "${include_dir}/AST/StructFieldRenamer.h"
  "${include_dir}/AST/StructGenerator.h"
  "${include_dir}/AST/SubprogramGenerator.h"
  "${include_dir}/AST/TransformVisitor.h"
  "${include_dir}/AST/Util.h"
  "${include_dir}/AST/Z3CondSimplify.h"
  "${include_dir}/AST/Z3ConvVisitor.h"
)

set(AST_COMPAT_HEADERS
  "${include_dir}/AST/Compat/ASTContext.h"
  "${include_dir}/AST/Compat/Mangle.h"
  "${include_dir}/AST/Compat/Stmt.h"
  "${include_dir}/AST/Compat/Type.h"
)

set(BC_HEADERS
  "${include_dir}/BC/Util.h"
  "${include_dir}/BC/Version.h"
)

set(BC_COMPAT_HEADERS
  "${include_dir}/BC/Compat/DerivedTypes.h"
  "${include_dir}/BC/Compat/Error.h"
  "${include_dir}/BC/Compat/IntrinsicInst.h"
  "${include_dir}/BC/Compat/IRReader.h"
  "${include_dir}/BC/Compat/Value.h"
  "${include_dir}/BC/Compat/Verifier.h"
)

set(VERSION_HEADERS
  "${include_dir}/Version.h"
)

set(public_HEADERS
  ${AST_HEADERS}
  ${AST_COMPAT_HEADERS}
  ${BC_HEADERS}
  ${BC_COMPAT_HEADERS}
  ${VERSION_HEADERS}
)

set(AST_COMPAT_SOURCES
  AST/Compat/ASTContext.cpp
  AST/Compat/Mangle.cpp
  AST/Compat/Stmt.cpp
)

set(AST_SOURCES
  AST/ASTBuilder.cpp
  AST/CXXToCDecl.cpp
  AST/InferenceRule.cpp
  AST/DeadStmtElim.cpp
  AST/DebugInfoCollector.cpp
  AST/CondBasedRefine.cpp
  AST/ExprCombine.cpp
  AST/GenerateAST.cpp
  AST/IRToASTVisitor.cpp
  AST/LocalDeclRenamer.cpp
  AST/LoopRefine.cpp
  AST/NestedCondProp.cpp
  AST/NestedScopeCombine.cpp
  AST/Util.cpp
  AST/Z3CondSimplify.cpp
  AST/Z3ConvVisitor.cpp
  AST/ReachBasedRefine.cpp
  AST/StructFieldRenamer.cpp
  AST/StructGenerator.cpp
  AST/SubprogramGenerator.cpp
)

set(BC_SOURCES
  BC/Util.cpp
)

set(BC_COMPAT_SOURCES
  BC/Compat/Value.cpp

  Decompiler.cpp
)

add_library("${PROJECT_NAME}" STATIC
  ${public_HEADERS}
  ${AST_SOURCES}
  ${AST_COMPAT_SOURCES}
  ${BC_SOURCES}
  ${BC_COMPAT_SOURCES}
  
  "${POST_CONFIGURE_FILE}"  # Version.cpp
)

#link version information
target_link_libraries("${PROJECT_NAME}"
  PUBLIC
    "${PROJECT_NAME}_cxx_settings"
    glog::glog
    llvm
    clangIndex
    clangCodeGen
    clangASTMatchers
    clangTooling
)

set_target_properties("${PROJECT_NAME}"
  PROPERTIES
    LINKER_LANGUAGE
      CXX
)

target_include_directories("${PROJECT_NAME}"
  PUBLIC
    $<BUILD_INTERFACE:${RELLIC_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(RELLIC_ENABLE_INSTALL)
  install(
    DIRECTORY
      "${include_dir}"
    DESTINATION
      "include")
  
  
  install(
    TARGETS
      "${PROJECT_NAME}"
    EXPORT
      "${PROJECT_NAME}Targets"
    RUNTIME
      DESTINATION
        "bin"
    LIBRARY
      DESTINATION
        "lib"
    ARCHIVE
      DESTINATION
        "lib"
  )
endif(RELLIC_ENABLE_INSTALL)
