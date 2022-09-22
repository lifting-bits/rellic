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
  "${include_dir}/AST/MaterializeConds.h"
  "${include_dir}/AST/NestedCondProp.h"
  "${include_dir}/AST/NestedScopeCombine.h"
  "${include_dir}/AST/ReachBasedRefine.h"
  "${include_dir}/AST/StructFieldRenamer.h"
  "${include_dir}/AST/StructGenerator.h"
  "${include_dir}/AST/SubprogramGenerator.h"
  "${include_dir}/AST/TransformVisitor.h"
  "${include_dir}/AST/TypeProvider.h"
  "${include_dir}/AST/Util.h"
  "${include_dir}/AST/Z3CondSimplify.h"
)

set(BC_HEADERS
  "${include_dir}/BC/Util.h"
  "${include_dir}/BC/Version.h"
)

set(VERSION_HEADERS
  "${include_dir}/Version.h"
)

set(public_HEADERS
  ${AST_HEADERS}
  ${BC_HEADERS}
  ${VERSION_HEADERS}
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
  AST/MaterializeConds.cpp
  AST/NestedCondProp.cpp
  AST/NestedScopeCombine.cpp
  AST/Util.cpp
  AST/Z3CondSimplify.cpp
  AST/ReachBasedRefine.cpp
  AST/StructFieldRenamer.cpp
  AST/StructGenerator.cpp
  AST/SubprogramGenerator.cpp
  AST/TypeProvider.cpp
)

set(BC_SOURCES
  BC/Util.cpp
)


add_library("${PROJECT_NAME}" STATIC
  ${public_HEADERS}
  ${AST_SOURCES}
  ${BC_SOURCES}

  Decompiler.cpp
  Exception.cpp
  
  "${POST_CONFIGURE_FILE}"  # Version.cpp
)


set_target_properties(
  "${PROJECT_NAME}" PROPERTIES
    VISIBILITY_INLINES_HIDDEN YES
    CXX_VISIBILITY_PRESET hidden
)

#link version information
target_link_libraries("${PROJECT_NAME}"
  PUBLIC
    "${PROJECT_NAME}_cxx_settings"
    glog::glog
    z3::libz3
    "${llvm_libs}"
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
  include(GNUInstallDirs)
  install(
    DIRECTORY
      "${include_dir}"
    DESTINATION
      ${CMAKE_INSTALL_INCLUDEDIR})
  
  
  install(
    TARGETS
      "${PROJECT_NAME}"
    EXPORT
      "${PROJECT_NAME}Targets"
    RUNTIME
      DESTINATION
        ${CMAKE_INSTALL_BINDIR}
    LIBRARY
      DESTINATION
        ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE
      DESTINATION
        ${CMAKE_INSTALL_LIBDIR}
  )
endif(RELLIC_ENABLE_INSTALL)
