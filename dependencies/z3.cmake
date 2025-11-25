# Z3 SMT Solver - required for rellic condition simplification

set(Z3_VERSION "4.13.0" CACHE STRING "Z3 version to build")
set(Z3_URL "https://github.com/Z3Prover/z3/archive/refs/tags/z3-${Z3_VERSION}.tar.gz")

set(Z3_ARGS
    "-DZ3_BUILD_LIBZ3_SHARED:BOOL=OFF"
    "-DZ3_BUILD_EXECUTABLE:BOOL=OFF"
    "-DZ3_BUILD_TEST_EXECUTABLES:BOOL=OFF"
    "-DZ3_ENABLE_EXAMPLE_TARGETS:BOOL=OFF"
    "-DZ3_BUILD_PYTHON_BINDINGS:BOOL=OFF"
    "-DZ3_BUILD_JAVA_BINDINGS:BOOL=OFF"
    "-DZ3_BUILD_DOTNET_BINDINGS:BOOL=OFF"
    "-DZ3_INCLUDE_GIT_HASH:BOOL=OFF"
    "-DZ3_INCLUDE_GIT_DESCRIBE:BOOL=OFF"
)

ExternalProject_Add(z3
    URL ${Z3_URL}
    CMAKE_CACHE_ARGS
        ${CMAKE_ARGS}
        ${Z3_ARGS}
    CMAKE_GENERATOR "Ninja"
)
