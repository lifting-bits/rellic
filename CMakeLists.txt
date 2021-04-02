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

if (DEFINED VCPKG_ROOT)
  message(WARNING "Building with VCPKG")
  include(cmake/vcpkg_helper.cmake)
else()
  message(WARNING "Running the standard CMake build; if this is a mistake, reconfigure again and pass `-DVCPKG_ROOT=/path/to/vcpkg`")
  set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")
endif()

project(rellic)
cmake_minimum_required(VERSION 3.2)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

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

#set(LLVM_LIBRARIES
#  LLVMCore LLVMSupport LLVMAnalysis LLVMipo LLVMIRReader
#  LLVMBitReader LLVMBitWriter LLVMTransformUtils LLVMScalarOpts
#  LLVMLTO
#)
set(LLVM_LIBRARIES LLVM)

list(APPEND PROJECT_LIBRARIES ${LLVM_LIBRARIES})
list(APPEND PROJECT_DEFINITIONS ${LLVM_DEFINITIONS})
list(APPEND PROJECT_INCLUDEDIRECTORIES ${LLVM_INCLUDE_DIRS})

# clang
find_package(Clang REQUIRED)
#set(CLANG_LIBS clangIndex clangCodeGen clangASTMatchers clangTooling)
set(CLANG_LIBS clang-cpp)
list(APPEND PROJECT_LIBRARIES ${CLANG_LIBS})

# z3
set(min_z3_version "4.7.1")
find_package(Z3 CONFIG ${min_z3_version} QUIET)
if (NOT Z3_FOUND)
  find_package(Z3 ${min_z3_version} REQUIRED)
  list(APPEND PROJECT_LIBRARIES Z3)
else()
  list(APPEND PROJECT_LIBRARIES z3::libz3)
endif()

# Potentially fixup bad LLVM z3 linkage
get_target_property(LLVMSupport_LIBS LLVMSupport INTERFACE_LINK_LIBRARIES)
list(FIND LLVMSupport_LIBS "Z3" _index)
if (${_index} GREATER -1)
  list(REMOVE_ITEM LLVMSupport_LIBS Z3)
  list(APPEND LLVMSupport_LIBS z3::libz3)
  set_target_properties(LLVMSupport PROPERTIES
    INTERFACE_LINK_LIBRARIES "${LLVMSupport_LIBS}")
endif()

# google log module
find_package(glog CONFIG QUIET)
if (NOT glog_FOUND)
  find_package(glog REQUIRED)
  list(APPEND PROJECT_LIBRARIES glog)
else()
  list(APPEND PROJECT_LIBRARIES glog::glog)
endif()

# gflags
find_package(gflags CONFIG QUIET)
if (NOT gflags_FOUND)
  find_package(gflags REQUIRED)
endif()
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

find_package(Clang CONFIG REQUIRED)
get_target_property(CLANG_PATH clang LOCATION)

message(STATUS "Clang path for tests: \"${CLANG_PATH}\"")

# Tests that survive a complete roundtrip
add_test(NAME test_roundtrip_rebuild
  COMMAND scripts/roundtrip.py $<TARGET_FILE:${RELLIC_DECOMP}> tests/tools/decomp/ "${CLANG_PATH}"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Tests that may not roundtrip yet, but should emit C
add_test(NAME test_roundtrip_translate_only
  COMMAND scripts/roundtrip.py --translate-only $<TARGET_FILE:${RELLIC_DECOMP}> tests/tools/decomp/failing-rebuild/ "${CLANG_PATH}"
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
