# Copyright (c) 2018 Trail of Bits, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

project(rellic)
cmake_minimum_required(VERSION 3.2)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/settings.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/utils.cmake")

# FindAndSelectClangCompiler()

set(RELLIC_SOURCE_DIR "${PROJECT_SOURCE_DIR}")

# warnings and compiler settings
if(NOT DEFINED WIN32)
  set(PROJECT_CXXFLAGS
    ${GLOBAL_CXXFLAGS}
    -Werror
    # -Wconversion
    -pedantic 
    -Wno-unreachable-code-return 
  )
endif()

list(APPEND PROJECT_INCLUDEDIRECTORIES ${CMAKE_CURRENT_BINARY_DIR})

#
# target settings
#

set(RELLIC_DECOMP rellic-decomp-${REMILL_LLVM_VERSION})

add_executable(${RELLIC_DECOMP}
  
  rellic/AST/CondBasedRefine.cpp
  rellic/AST/DeadStmtElim.cpp
  rellic/AST/GenerateAST.cpp
  rellic/AST/IRToASTVisitor.cpp
  rellic/AST/LoopRefine.cpp
  rellic/AST/NestedCondProp.cpp
  rellic/AST/NestedScopeCombiner.cpp
  rellic/AST/Util.cpp
  rellic/AST/Z3CondSimplify.cpp
  rellic/AST/Z3ConvVisitor.cpp
  
  tools/decomp/Decomp.cpp
)

# this is needed for the #include directives with absolutes paths to work correctly; it must
# also be set to PUBLIC since rellic-lift includes some files directly
list(APPEND PROJECT_INCLUDEDIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR})

#
# libraries
#

# remill
if(NOT TARGET remill)
  if("${PLATFORM_NAME}" STREQUAL "windows")
    set(REMILL_FINDPACKAGE_HINTS HINTS "${CMAKE_INSTALL_PREFIX}/remill/lib")
  endif()

  find_package(remill REQUIRED ${REMILL_FINDPACKAGE_HINTS})
endif()

list(APPEND PROJECT_LIBRARIES remill)

# llvm
find_package(LLVM REQUIRED CONFIG HINTS ${FINDPACKAGE_LLVM_HINTS})

string(REPLACE "." ";" LLVM_VERSION_LIST ${LLVM_PACKAGE_VERSION})
list(GET LLVM_VERSION_LIST 0 LLVM_MAJOR_VERSION)
list(GET LLVM_VERSION_LIST 1 LLVM_MINOR_VERSION)

set(LLVM_LIBRARIES
  LLVMCore LLVMSupport LLVMAnalysis LLVMipo LLVMIRReader
  LLVMBitReader LLVMBitWriter LLVMTransformUtils LLVMScalarOpts
  LLVMLTO
)

list(APPEND PROJECT_LIBRARIES ${LLVM_LIBRARIES})
list(APPEND PROJECT_DEFINITIONS ${LLVM_DEFINITIONS})
list(APPEND PROJECT_INCLUDEDIRECTORIES ${LLVM_INCLUDE_DIRS})

# clang
find_package(Clang REQUIRED)
set(CLANG_LIBS clangIndex clangCodeGen clangASTMatchers)
list(APPEND PROJECT_LIBRARIES ${CLANG_LIBS})

# z3
find_package(Z3 REQUIRED)
list(APPEND PROJECT_INCLUDEDIRECTORIES ${Z3_INCLUDE_DIRS})
list(APPEND PROJECT_LIBRARIES ${Z3_LIBRARIES})

# google log module
find_package(glog REQUIRED)
list(APPEND PROJECT_LIBRARIES glog::glog)

# gflags
find_package(gflags REQUIRED)
list(APPEND PROJECT_LIBRARIES gflags)

#
# target settings
#

target_link_libraries(${RELLIC_DECOMP} PRIVATE ${PROJECT_LIBRARIES})
target_include_directories(${RELLIC_DECOMP} SYSTEM PUBLIC ${PROJECT_INCLUDEDIRECTORIES})
target_compile_definitions(${RELLIC_DECOMP} PUBLIC ${PROJECT_DEFINITIONS})
target_compile_options(${RELLIC_DECOMP} PRIVATE ${PROJECT_CXXFLAGS})