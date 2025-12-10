# Rellic Dependencies Superbuild

This directory contains the superbuild configuration for Rellic's dependencies, based on [remill's pattern](https://github.com/lifting-bits/remill/tree/master/dependencies).

## Building with External LLVM (Recommended)

### Linux

```sh
# Install LLVM 20 from apt.llvm.org
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 20

sudo apt install llvm-20-dev clang-20 libclang-20-dev cmake ninja-build zlib1g-dev libzstd-dev

# Build dependencies
cmake -G Ninja -S dependencies -B dependencies/build \
    -DUSE_EXTERNAL_LLVM=ON \
    -DCMAKE_PREFIX_PATH="$(llvm-config-20 --cmakedir)/.."
cmake --build dependencies/build
```

### macOS

> **Note:** Using Homebrew's pre-built LLVM on macOS has known ABI compatibility issues
> that cause runtime crashes. We recommend building LLVM from source on macOS
> (see "Full Superbuild" below). This builds LLVM with the same compiler used
> for rellic, ensuring ABI compatibility.

If you want to try Homebrew LLVM anyway (build may succeed but runtime crashes likely):

```sh
brew install llvm@20 ninja

# Build dependencies
cmake -G Ninja -S dependencies -B dependencies/build \
    -DUSE_EXTERNAL_LLVM=ON \
    -DCMAKE_PREFIX_PATH="$(brew --prefix llvm@20)"
cmake --build dependencies/build
```

## Building rellic

After building dependencies, build rellic with:

### Linux

```sh
cmake -G Ninja -B build \
    -DCMAKE_PREFIX_PATH="$PWD/dependencies/install;$(llvm-config-20 --cmakedir)/.." \
    -DCMAKE_INSTALL_PREFIX="$PWD/install"
cmake --build build
cmake --install build
ctest --test-dir build --output-on-failure
```

### macOS (from source build)

After using the full superbuild to build LLVM from source:

```sh
cmake -G Ninja -B build \
    -DCMAKE_PREFIX_PATH="$PWD/dependencies/install" \
    -DCMAKE_INSTALL_PREFIX="$PWD/install"
cmake --build build
cmake --install build
ctest --test-dir build --output-on-failure
```

## Full Superbuild (including LLVM)

If you want to build LLVM from source (recommended for macOS):

```sh
cmake -G Ninja -S dependencies -B dependencies/build \
    -DCMAKE_INSTALL_PREFIX="$PWD/dependencies/install"
cmake --build dependencies/build

cmake -G Ninja -B build \
    -DCMAKE_PREFIX_PATH="$PWD/dependencies/install" \
    -DCMAKE_INSTALL_PREFIX="$PWD/install"
cmake --build build
cmake --install build
ctest --test-dir build --output-on-failure
```

**Note:** Building LLVM from source takes significant time (30-60 minutes or more).

## Using External Z3

If you want to use a system-installed Z3 instead of building it:

```sh
sudo apt install libz3-dev  # Linux
# or
brew install z3  # macOS

cmake -G Ninja -S dependencies -B dependencies/build \
    -DUSE_EXTERNAL_LLVM=ON \
    -DUSE_EXTERNAL_Z3=ON
cmake --build dependencies/build
```

## Dependencies Built

This superbuild builds the following dependencies:

- **gflags** (v52e9456) - Command-line flags library
- **glog** (v0.7.1) - Google logging library
- **googletest** (v1.17.0) - Google Test framework
- **doctest** (v2.4.11) - Unit testing framework
- **cpp-httplib** (v0.15.3) - HTTP library for rellic-xref
- **Z3** (v4.13.0) - SMT solver (unless USE_EXTERNAL_Z3=ON)
- **LLVM** (configurable) - LLVM compiler infrastructure (unless USE_EXTERNAL_LLVM=ON)

## Supported LLVM Versions

Currently only **LLVM 20** is supported. The codebase uses LLVM 20-specific APIs.


## Troubleshooting

### Ninja not found

```sh
sudo apt install ninja-build  # Linux
brew install ninja  # macOS
```

### macOS runtime crashes with Homebrew LLVM

If the build succeeds but rellic crashes at runtime with SIGSEGV in
`llvm::AnalysisManager<llvm::Module>::getResultImpl`, this is due to how Homebrew
builds LLVM as a **shared library** (`libLLVM.dylib`).

The crash occurs because rellic registers custom analyses with LLVM's new pass manager.
When LLVM is built as a shared library, the `AnalysisKey` addresses don't match across
the shared library boundary, causing crashes when the pass manager tries to look up
analyses.

The solution is to build LLVM from source with static libraries (the default for our
superbuild). See "Full Superbuild" above.

**Technical details:** Homebrew builds LLVM with `LLVM_BUILD_LLVM_DYLIB=ON` and
`LLVM_LINK_LLVM_DYLIB=ON`. This causes `AnalysisManager::getResultImpl` to be
instantiated in libLLVM.dylib, but the `AnalysisKey` for rellic's `GenerateAST`
pass is in the rellic binary. When the manager searches for the key, it doesn't
find it because the key addresses differ between the dylib and the application.

### Z3 build fails

Use an external Z3 installation:

```sh
cmake -G Ninja -S dependencies -B dependencies/build \
    -DUSE_EXTERNAL_LLVM=ON \
    -DUSE_EXTERNAL_Z3=ON
```
