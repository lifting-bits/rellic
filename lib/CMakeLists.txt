#
# Copyright (c) 2020-present, Trail of Bits, Inc.
# All rights reserved.
#
# This source code is licensed in accordance with the terms specified in
# the LICENSE file found in the root directory of this source tree.
#


#define the "Version.cpp" in the binary directory.
add_subdirectory(Version)

set(include_dir "${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}")

set(AST_HEADERS
  "${include_dir}/AST/ASTBuilder.h"
  "${include_dir}/AST/ASTPrinter.h"
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
  "${include_dir}/Version/Version.h"
)

set(public_HEADERS
  ${AST_HEADERS}
  ${AST_COMPAT_HEADERS}
  ${BC_HEADERS}
  ${BC_COMPAT_HEADERS}
  ${VERSION_HEADERS}
)

add_library("${PROJECT_NAME}" STATIC
  ${public_HEADERS}

  AST/Compat/ASTContext.cpp
  AST/Compat/Mangle.cpp
  AST/Compat/Stmt.cpp
  
  AST/ASTBuilder.cpp
  AST/ASTPrinter.cpp
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
  
  BC/Util.cpp
  BC/Compat/Value.cpp
  
  "${CMAKE_CURRENT_BINARY_DIR}/Version/Version.cpp"
)

#link version information
target_link_libraries("${PROJECT_NAME}"
  PUBLIC
    glog::glog
    gflags::gflags
    llvm
    clangIndex clangCodeGen clangASTMatchers clangTooling)

target_compile_features("${PROJECT_NAME}"
  PUBLIC
    cxx_std_17)

set_target_properties("${PROJECT_NAME}"
  PROPERTIES
    LINKER_LANGUAGE
      CXX
    PUBLIC_HEADER
      "${public_HEADERS}"
)

add_project_properties("${PROJECT_NAME}")

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
