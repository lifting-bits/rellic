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

# Fix behavior of CMAKE_CXX_STANDARD when targeting macOS.
if (POLICY CMP0025)
  cmake_policy(SET CMP0025 NEW)
endif ()

project(rellic)
cmake_minimum_required(VERSION 3.2)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/settings.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/utils.cmake")

FindAndSelectClangCompiler()

enable_language(C CXX ASM)

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

# this is needed for the #include directives with absolutes paths to work correctly; it must
# also be set to PUBLIC since rellic-lift includes some files directly
list(APPEND PROJECT_INCLUDEDIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR})

#
# libraries
#

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
set(CLANG_LIBS clangIndex clangCodeGen clangASTMatchers clangTooling)
list(APPEND PROJECT_LIBRARIES ${CLANG_LIBS})

# z3
if (DEFINED Z3_INSTALL_PREFIX)
  # acquire OpenMP for z3
  find_package(OpenMP REQUIRED)
  # import libz3.a
  add_library(z3 STATIC IMPORTED)
  set_target_properties(z3 PROPERTIES IMPORTED_LOCATION ${Z3_INSTALL_PREFIX}/bin/libz3.a)
  # set variables
  list(APPEND PROJECT_INCLUDEDIRECTORIES ${Z3_INSTALL_PREFIX}/include)
  list(APPEND PROJECT_LIBRARIES OpenMP::OpenMP_CXX z3)
else ()
  find_package(Z3 REQUIRED)
  list(APPEND PROJECT_INCLUDEDIRECTORIES ${Z3_INCLUDE_DIRS})
  list(APPEND PROJECT_LIBRARIES ${Z3_LIBRARIES})
endif()

# google log module
find_package(glog REQUIRED)
list(APPEND PROJECT_LIBRARIES glog::glog)

# gflags
find_package(gflags REQUIRED)
list(APPEND PROJECT_LIBRARIES gflags)

set(RELLIC_LLVM_VERSION "${LLVM_MAJOR_VERSION}.${LLVM_MINOR_VERSION}")

#
# helper macro to set target properties
#

macro(add_project_properties target)
  target_link_libraries(${target} PRIVATE ${PROJECT_LIBRARIES})
  target_include_directories(${target} SYSTEM PUBLIC ${PROJECT_INCLUDEDIRECTORIES})
  target_compile_definitions(${target} PUBLIC ${PROJECT_DEFINITIONS})
  target_compile_options(${target} PRIVATE ${PROJECT_CXXFLAGS})
endmacro()

#
# rellic libraries
#

add_subdirectory(rellic)

#
# rellic executables
#

add_subdirectory(tools)

#
# tests
#

enable_testing()

add_test(NAME test_roundtrip
  COMMAND python3.7 scripts/roundtrip.py $<TARGET_FILE:${RELLIC_DECOMP}> tests/tools/decomp/ ${LIBRARY_REPOSITORY_ROOT}/llvm/bin/clang
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)