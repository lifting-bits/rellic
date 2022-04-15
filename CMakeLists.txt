#
# Copyright (c) 2020-present, Trail of Bits, Inc.
# All rights reserved.
#
# This source code is licensed in accordance with the terms specified in
# the LICENSE file found in the root directory of this source tree.
#

cmake_minimum_required(VERSION 3.21)

include("cmake/options.cmake")
include("cmake/vcpkg_helper.cmake")

project(rellic)
set(CMAKE_CXX_VISIBILITY_PRESET hidden) 
set(CMAKE_C_VISIBILITY_PRESET hidden) 
set (VISIBILITY_INLINES_HIDDEN YES)
include("cmake/settings.cmake")
include("cmake/llvm.cmake")
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

find_package(gflags CONFIG REQUIRED)
find_package(glog CONFIG REQUIRED)
find_package(Z3 4.8 CONFIG REQUIRED)
find_llvm(llvm)
find_package(Clang CONFIG REQUIRED)


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
      "llvm" "${PROJECT_NAME}_cxx_settings"
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

  set (TESTS_OUTPUT_PATH "${CMAKE_CURRENT_BINARY_DIR}/rellic_tests")

  add_custom_target(create_tar ALL COMMAND
      ${CMAKE_COMMAND} -E tar "cfvz" "rellic_tests.tgz" "${TESTS_OUTPUT_PATH}")

  file(GLOB RELLIC_DECOMP_TESTS "${CMAKE_SOURCE_DIR}/tests/tools/decomp/*.c")

  foreach(OPTLEVEL IN ITEMS O0 O1 O2 O3)
    file(MAKE_DIRECTORY "${TESTS_OUTPUT_PATH}/comp_${OPTLEVEL}")
    file(MAKE_DIRECTORY "${TESTS_OUTPUT_PATH}/decomp_${OPTLEVEL}")
    file(MAKE_DIRECTORY "${TESTS_OUTPUT_PATH}/recomp_${OPTLEVEL}")
    file(MAKE_DIRECTORY "${TESTS_OUTPUT_PATH}/asm_${OPTLEVEL}")

    foreach(TEST ${RELLIC_DECOMP_TESTS})
      cmake_path(GET TEST STEM TESTNAME)

      add_custom_target("compile_test_${TESTNAME}_${OPTLEVEL}"
        COMMAND
          "${CLANG_PATH}" -S -emit-llvm -"${OPTLEVEL}" "${TEST}" -o "${TESTS_OUTPUT_PATH}/comp_${OPTLEVEL}/${TESTNAME}.ll"
      )

      add_custom_target("decompile_test_${TESTNAME}_${OPTLEVEL}"
        COMMAND
          "$<TARGET_FILE:${RELLIC_DECOMP}>" --logtostderr=1 --input "${TESTS_OUTPUT_PATH}/comp_${OPTLEVEL}/${TESTNAME}.ll" --output "${TESTS_OUTPUT_PATH}/decomp_${OPTLEVEL}/${TESTNAME}.c"
      )

      add_custom_target("recompile_test_${TESTNAME}_${OPTLEVEL}"
        COMMAND
        "${CLANG_PATH}" -S -emit-llvm "${TESTS_OUTPUT_PATH}/decomp_${OPTLEVEL}/${TESTNAME}.c" -o "${TESTS_OUTPUT_PATH}/recomp_${OPTLEVEL}/${TESTNAME}.ll"
      )

      add_custom_target("recompile_asm_test_${TESTNAME}_${OPTLEVEL}"
        COMMAND
        "${CLANG_PATH}" -S "${TESTS_OUTPUT_PATH}/decomp_${OPTLEVEL}/${TESTNAME}.c" -o "${TESTS_OUTPUT_PATH}/asm_${OPTLEVEL}/${TESTNAME}.S"
      )

      add_dependencies(create_tar "compile_test_${TESTNAME}_${OPTLEVEL}" "decompile_test_${TESTNAME}_${OPTLEVEL}" "recompile_test_${TESTNAME}_${OPTLEVEL}" "recompile_asm_test_${TESTNAME}_${OPTLEVEL}")
      add_dependencies("recompile_test_${TESTNAME}_${OPTLEVEL}" "decompile_test_${TESTNAME}_${OPTLEVEL}")
      add_dependencies("recompile_asm_test_${TESTNAME}_${OPTLEVEL}" "decompile_test_${TESTNAME}_${OPTLEVEL}")
      add_dependencies("decompile_test_${TESTNAME}_${OPTLEVEL}" "${RELLIC_DECOMP}")
    endforeach()
  endforeach()

  # Tests that survive a complete roundtrip
  add_test(NAME test_roundtrip_rebuild
    COMMAND "${Python3_EXECUTABLE}" scripts/roundtrip.py $<TARGET_FILE:${RELLIC_DECOMP}> tests/tools/decomp/ "${CLANG_PATH}" --timeout 30
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  # Tests that may not roundtrip yet, but should emit C
  add_test(NAME test_roundtrip_translate_only
    COMMAND "${Python3_EXECUTABLE}" scripts/roundtrip.py --translate-only $<TARGET_FILE:${RELLIC_DECOMP}> tests/tools/decomp/failing-rebuild/ "${CLANG_PATH}" --timeout 30
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  # Tests that can be decompiled
  add_test(NAME test_decompile
    COMMAND "${Python3_EXECUTABLE}" scripts/decompile.py $<TARGET_FILE:${RELLIC_DECOMP}> tests/tools/decomp/ --timeout 30
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  add_test(NAME test_headergen
    COMMAND "${Python3_EXECUTABLE}" scripts/test-headergen.py $<TARGET_FILE:${RELLIC_HEADERGEN}> tests/tools/headergen/ "${CLANG_PATH}"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )
endif()
