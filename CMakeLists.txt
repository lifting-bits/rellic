#
# Copyright (c) 2020-present, Trail of Bits, Inc.
# All rights reserved.
#
# This source code is licensed in accordance with the terms specified in
# the LICENSE file found in the root directory of this source tree.
#

include("cmake/vcpkg_helper.cmake")

project(rellic)
cmake_minimum_required(VERSION 3.19)

include("cmake/options.cmake")
include("cmake/settings.cmake")
include("cmake/llvm.cmake")

include(GNUInstallDirs)

if(RELLIC_ENABLE_INSTALL)
  include("cmake/packaging.cmake")
endif(RELLIC_ENABLE_INSTALL)

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

# list(APPEND PROJECT_INCLUDEDIRECTORIES ${CMAKE_CURRENT_BINARY_DIR})

# this is needed for the #include directives with absolutes paths to work correctly; it must
# also be set to PUBLIC since rellic-lift includes some files directly
list(APPEND PROJECT_INCLUDEDIRECTORIES $<BUILD_INTERFACE:${RELLIC_SOURCE_DIR}/include>)
list(APPEND PROJECT_INCLUDEDIRECTORIES $<INSTALL_INTERFACE:include>)

#
# libraries
#

set(_VCPKG_SHARE_DIR "${VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/${CMAKE_INSTALL_DATADIR}")

find_package(gflags CONFIG REQUIRED HINTS "${_VCPKG_SHARE_DIR}")
find_package(glog CONFIG REQUIRED HINTS "${_VCPKG_SHARE_DIR}")
find_llvm(llvm "${_VCPKG_SHARE_DIR}")

# LLVM uses `-lZ3` somewhere, so this is a good way to sneak it in.
# NOTE: Recent versions of cxx-common vcpkg llvm dependencies fix this
if(NOT Z3_FOUND)
    find_package(Z3 4.8 CONFIG REQUIRED)
    if (NOT TARGET Z3)
        add_library(Z3 INTERFACE)
    endif()
    target_link_libraries(Z3 INTERFACE z3::libz3)
endif()

find_package(Clang CONFIG REQUIRED)


#
# helper macro to set target properties
#

macro(add_project_properties target)
  target_link_libraries(${target} PRIVATE ${PROJECT_LIBRARIES})
  target_include_directories(${target} SYSTEM PUBLIC ${PROJECT_INCLUDEDIRECTORIES})
  target_compile_definitions(${target} PUBLIC ${PROJECT_DEFINITIONS})
  target_compile_options(${target} PRIVATE ${PROJECT_CXXFLAGS})
endmacro()

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
      "llvm"
    EXPORT
      "${PROJECT_NAME}Targets"
  )
endif(RELLIC_ENABLE_INSTALL)

#
# external libraries
#

add_subdirectory(external)

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


  # Tests that survive a complete roundtrip
  add_test(NAME test_roundtrip_rebuild
    COMMAND "${Python3_EXECUTABLE}" scripts/roundtrip.py $<TARGET_FILE:${RELLIC_DECOMP}> tests/tools/decomp/ "${CLANG_PATH}"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  # Tests that may not roundtrip yet, but should emit C
  add_test(NAME test_roundtrip_translate_only
    COMMAND "${Python3_EXECUTABLE}" scripts/roundtrip.py --translate-only $<TARGET_FILE:${RELLIC_DECOMP}> tests/tools/decomp/failing-rebuild/ "${CLANG_PATH}"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  # Tests that can be decompiled
  add_test(NAME test_decompile
    COMMAND "${Python3_EXECUTABLE}" scripts/decompile.py $<TARGET_FILE:${RELLIC_DECOMP}> tests/tools/decomp/
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  add_test(NAME test_headergen
    COMMAND "${Python3_EXECUTABLE}" scripts/test-headergen.py $<TARGET_FILE:${RELLIC_HEADERGEN}> tests/tools/headergen/ "${CLANG_PATH}"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
endif()
