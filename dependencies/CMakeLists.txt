# https://alexreinking.com/blog/how-to-use-cmake-without-the-agonizing-pain-part-1.html
cmake_minimum_required(VERSION 3.21)

# Default to a Release config. Required before project() because Windows defaults to Debug
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "")
if(CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

project(dependencies)

option(USE_EXTERNAL_LLVM "Use system LLVM instead of building" OFF)
option(USE_EXTERNAL_Z3 "Use system Z3 instead of building" OFF)
option(USE_SANITIZERS "Use ASan and UBSan" OFF)

# Detect Homebrew LLVM on macOS
if(USE_EXTERNAL_LLVM)
    if(CMAKE_HOST_SYSTEM_NAME MATCHES "Darwin" AND NOT CMAKE_PREFIX_PATH)
        execute_process(
            COMMAND brew --prefix llvm
            RESULT_VARIABLE BREW_LLVM
            OUTPUT_VARIABLE BREW_LLVM_PREFIX
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(BREW_LLVM EQUAL 0 AND EXISTS "${BREW_LLVM_PREFIX}")
            set(CMAKE_PREFIX_PATH "${BREW_LLVM_PREFIX}")
            message(STATUS "Found Homebrew LLVM at ${BREW_LLVM_PREFIX}")
        else()
            message(WARNING "LLVM not found, to install: brew install llvm")
        endif()
    endif()
    find_package(LLVM CONFIG REQUIRED)
    message(STATUS "LLVM ${LLVM_PACKAGE_VERSION}: ${LLVM_DIR}")
endif()

if(USE_SANITIZERS)
    list(APPEND CMAKE_C_FLAGS "-fsanitize=address,undefined")
    list(APPEND CMAKE_CXX_FLAGS "-fsanitize=address,undefined")
endif()

include(superbuild.cmake)

# Build dependencies in order (implicit DEPENDS via superbuild.cmake)
simple_git(https://github.com/gflags/gflags 52e94563eba1968783864942fedf6e87e3c611f4)

simple_git(https://github.com/google/glog v0.7.1
    "-DGFLAGS_USE_TARGET_NAMESPACE:STRING=ON"
    "-DBUILD_TESTING:STRING=OFF"
)

simple_git(https://github.com/google/googletest v1.17.0
    "-Dgtest_force_shared_crt:STRING=ON"
    "-DGFLAGS_USE_TARGET_NAMESPACE:STRING=ON"
)

simple_git(https://github.com/doctest/doctest v2.4.12
    "-DDOCTEST_WITH_TESTS:STRING=OFF"
)

simple_git(https://github.com/yhirose/cpp-httplib v0.15.3
    "-DHTTPLIB_COMPILE:BOOL=OFF"
    "-DHTTPLIB_REQUIRE_OPENSSL:BOOL=OFF"
)

# Z3 (rellic-specific)
if(NOT USE_EXTERNAL_Z3)
    include(z3.cmake)
endif()

# LLVM (optional, can use external)
if(NOT USE_EXTERNAL_LLVM)
    include(llvm.cmake)
endif()
