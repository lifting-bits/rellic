#
# Copyright (c) 2020-present, Trail of Bits, Inc.
# All rights reserved.
#
# This source code is licensed in accordance with the terms specified in
# the LICENSE file found in the root directory of this source tree.
#

cmake_minimum_required(VERSION 3.21)

include("cmake/ccache.cmake")
include("cmake/options.cmake")

project(rellic)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set (VISIBILITY_INLINES_HIDDEN YES)
include("cmake/settings.cmake")
include(GNUInstallDirs)

if(RELLIC_ENABLE_INSTALL)
  include("cmake/packaging.cmake")
endif(RELLIC_ENABLE_INSTALL)

enable_language(C CXX ASM)

set(RELLIC_SOURCE_DIR "${PROJECT_SOURCE_DIR}")

add_library("${PROJECT_NAME}_cxx_settings" INTERFACE)
target_compile_features("${PROJECT_NAME}_cxx_settings"
  INTERFACE
    cxx_std_17
)

# warnings and compiler settings
if(NOT DEFINED WIN32)
  target_compile_options("${PROJECT_NAME}_cxx_settings"
    INTERFACE
      -Werror
      # -Wconversion
      -pedantic
      -Wno-unreachable-code-return
  )
endif(NOT DEFINED WIN32)

#
# libraries
#

# Currently only LLVM 20 is supported (code uses LLVM 20 specific APIs)
set(RELLIC_SUPPORTED_LLVM_VERSIONS 20)

# gflags needs this to create the gflags::gflags target that glog expects
set(GFLAGS_USE_TARGET_NAMESPACE ON)
find_package(gflags CONFIG REQUIRED)
find_package(glog CONFIG REQUIRED)
find_package(Z3 CONFIG REQUIRED)
find_package(doctest CONFIG REQUIRED)
find_package(LLVM CONFIG REQUIRED)

# Validate LLVM version
if(NOT LLVM_VERSION_MAJOR IN_LIST RELLIC_SUPPORTED_LLVM_VERSIONS)
    message(FATAL_ERROR
        "LLVM ${LLVM_VERSION_MAJOR} is not supported. "
        "Supported versions: ${RELLIC_SUPPORTED_LLVM_VERSIONS}")
endif()

llvm_map_components_to_libnames(llvm_libs support core irreader bitreader bitwriter)

# Check if we need to force static LLVM linking (for Homebrew LLVM on macOS)
# When LLVM is built with LLVM_LINK_LLVM_DYLIB=ON, Clang libraries link to libLLVM.dylib
# which causes crashes due to AnalysisKey address mismatches across shared library boundaries
option(RELLIC_FORCE_STATIC_LLVM "Force static LLVM linking even when dylib is available" OFF)

if(LLVM_LINK_LLVM_DYLIB AND RELLIC_FORCE_STATIC_LLVM)
    message(STATUS "LLVM built with dylib but RELLIC_FORCE_STATIC_LLVM is ON - will use static LLVM libraries")

    # Get all static LLVM libraries using llvm-config
    execute_process(
        COMMAND "${LLVM_TOOLS_BINARY_DIR}/llvm-config" --link-static --libnames
        OUTPUT_VARIABLE LLVM_STATIC_LIB_NAMES
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    # Convert space-separated list to CMake list
    string(REPLACE " " ";" LLVM_STATIC_LIB_NAMES_LIST "${LLVM_STATIC_LIB_NAMES}")

    # Convert library names (libLLVMFoo.a) to target names (LLVMFoo)
    set(LLVM_STATIC_TARGETS "")
    foreach(lib ${LLVM_STATIC_LIB_NAMES_LIST})
        # Extract target name: libLLVMFoo.a -> LLVMFoo
        string(REGEX REPLACE "^lib(.+)\\.a$" "\\1" target_name "${lib}")
        if(TARGET ${target_name})
            list(APPEND LLVM_STATIC_TARGETS ${target_name})
        endif()
    endforeach()

    set(RELLIC_LLVM_STATIC_TARGETS "${LLVM_STATIC_TARGETS}")
endif()

find_package(Clang CONFIG REQUIRED)

# If using static LLVM, patch ALL Clang targets to not use LLVM dylib
if(LLVM_LINK_LLVM_DYLIB AND RELLIC_FORCE_STATIC_LLVM AND RELLIC_LLVM_STATIC_TARGETS)
    message(STATUS "Patching Clang targets to use static LLVM libraries")

    # Get all Clang targets - we need to patch ALL of them
    # These are all the Clang libraries that might reference LLVM
    set(CLANG_TARGETS_TO_PATCH
        clangBasic clangAPINotes clangLex clangParse clangAST
        clangDynamicASTMatchers clangASTMatchers clangCrossTU clangSema
        clangCodeGen clangAnalysis clangAnalysisFlowSensitive
        clangAnalysisFlowSensitiveModels clangEdit clangExtractAPI
        clangRewrite clangARCMigrate clangDriver clangSerialization
        clangRewriteFrontend clangFrontend clangFrontendTool
        clangToolingCore clangToolingInclusions clangToolingInclusionsStdlib
        clangToolingRefactoring clangToolingASTDiff clangToolingSyntax
        clangDependencyScanning clangTransformer clangTooling
        clangDirectoryWatcher clangIndex clangIndexSerialization
        clangInstallAPI clangStaticAnalyzerCore clangStaticAnalyzerCheckers
        clangStaticAnalyzerFrontend clangFormat clangInterpreter clangSupport
    )

    foreach(clang_target ${CLANG_TARGETS_TO_PATCH})
        if(TARGET ${clang_target})
            # Get current interface link libraries
            get_target_property(current_libs ${clang_target} INTERFACE_LINK_LIBRARIES)
            if(current_libs)
                # Check if LLVM is in the list (as exact match, not substring)
                list(FIND current_libs "LLVM" llvm_index)
                if(NOT llvm_index EQUAL -1)
                    # Remove LLVM and add all static targets
                    list(REMOVE_ITEM current_libs "LLVM")
                    list(APPEND current_libs ${RELLIC_LLVM_STATIC_TARGETS})
                    set_target_properties(${clang_target} PROPERTIES
                        INTERFACE_LINK_LIBRARIES "${current_libs}"
                    )
                endif()
            endif()
        endif()
    endforeach()

    # Also patch the LLVM target itself to be an interface to static libs
    # This handles any targets we might have missed
    set_target_properties(LLVM PROPERTIES
        INTERFACE_LINK_LIBRARIES "${RELLIC_LLVM_STATIC_TARGETS}"
    )
endif()

# Extract LLVM version for binary naming
set(RELLIC_LLVM_VERSION "${LLVM_VERSION_MAJOR}")
message(STATUS "Building rellic for LLVM ${RELLIC_LLVM_VERSION}")


#
# helper macro to set target properties
#

if(RELLIC_ENABLE_INSTALL)
  export(PACKAGE "${PROJECT_NAME}")

  set(cmake_install_dir "lib/cmake/${PROJECT_NAME}")

  include(CMakePackageConfigHelpers)
  configure_package_config_file("${PROJECT_NAME}Config.cmake.in"
      "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
      INSTALL_DESTINATION "${cmake_install_dir}"
  )

  install(
      FILES
          "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
      DESTINATION "${cmake_install_dir}"
  )
  install(EXPORT "${PROJECT_NAME}Targets"
      DESTINATION "${cmake_install_dir}"
      NAMESPACE "${PROJECT_NAME}::"
  )
  install(
    TARGETS
      "${PROJECT_NAME}_cxx_settings"
    EXPORT
      "${PROJECT_NAME}Targets"
  )
endif(RELLIC_ENABLE_INSTALL)

#
# rellic libraries
#

add_subdirectory(lib)

#
# rellic executables
#

add_subdirectory(tools)

#
# tests
#


if (RELLIC_ENABLE_TESTING)
  enable_testing()

  add_subdirectory(unittests)

  get_target_property(CLANG_PATH clang LOCATION)
  message(STATUS "Clang path for tests: \"${CLANG_PATH}\"")

  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  message(STATUS "Python path for tests: \"${Python3_EXECUTABLE}\"")

  # Only pass sysroot to tests if it's a non-empty string
  if(CMAKE_OSX_SYSROOT)
    set(RELLIC_TEST_ARGS "--cflags=-isysroot" "--cflags=${CMAKE_OSX_SYSROOT}")
  else()
    set(RELLIC_TEST_ARGS "")
  endif()

  # Tests that survive a complete roundtrip
  add_test(NAME test_roundtrip_rebuild
    COMMAND "${Python3_EXECUTABLE}" scripts/roundtrip.py $<TARGET_FILE:${RELLIC_DECOMP}> tests/tools/decomp/ "${CLANG_PATH}" --timeout 30 ${RELLIC_TEST_ARGS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  # Tests that may not roundtrip yet, but should emit C
  add_test(NAME test_roundtrip_translate_only
    COMMAND "${Python3_EXECUTABLE}" scripts/roundtrip.py --translate-only $<TARGET_FILE:${RELLIC_DECOMP}> tests/tools/decomp/failing-rebuild/ "${CLANG_PATH}" --timeout 30 ${RELLIC_TEST_ARGS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  # Tests that can be decompiled
  add_test(NAME test_decompile
    COMMAND "${Python3_EXECUTABLE}" scripts/decompile.py $<TARGET_FILE:${RELLIC_DECOMP}> tests/tools/decomp/ --timeout 30
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  add_test(NAME test_headergen
    COMMAND "${Python3_EXECUTABLE}" scripts/test-headergen.py $<TARGET_FILE:${RELLIC_HEADERGEN}> tests/tools/headergen/ "${CLANG_PATH}" ${RELLIC_TEST_ARGS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
endif()
