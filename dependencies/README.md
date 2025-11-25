# Rellic Dependencies Superbuild

This directory contains the superbuild configuration for Rellic's dependencies, based on [remill's pattern](https://github.com/lifting-bits/remill/tree/master/dependencies).

## Building with External LLVM (Recommended)

### Linux

```sh
# Install LLVM from apt.llvm.org (choose version 16, 17, 18, 19, or 20)
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 17  # Replace 17 with your desired version

sudo apt install llvm-17-dev clang-17 libclang-17-dev cmake ninja-build

# Build dependencies
cmake -G Ninja -S dependencies -B dependencies/build -DUSE_EXTERNAL_LLVM=ON
cmake --build dependencies/build
```

### macOS

```sh
# Install LLVM from Homebrew (choose version 16, 17, 18, 19, or 20)
brew install llvm@17 ninja  # Replace 17 with your desired version

# Build dependencies
cmake -G Ninja -S dependencies -B dependencies/build -DUSE_EXTERNAL_LLVM=ON
cmake --build dependencies/build
```

## Building rellic

After building dependencies, build rellic with:

### Linux

```sh
cmake -G Ninja -B build \
    "-DCMAKE_PREFIX_PATH=$PWD/dependencies/install;$(llvm-config-17 --prefix)" \
    "-DCMAKE_INSTALL_PREFIX=$PWD/install"
cmake --build build
cmake --install build
```

### macOS

```sh
cmake -G Ninja -B build \
    "-DCMAKE_PREFIX_PATH=$PWD/dependencies/install;$(brew --prefix llvm@17)" \
    "-DCMAKE_INSTALL_PREFIX=$PWD/install"
cmake --build build
cmake --install build
```

## Full Superbuild (including LLVM)

If you want to build LLVM from source instead of using a system installation:

```sh
cmake -G Ninja -S dependencies -B dependencies/build \
    -DLLVM_URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-17.0.6/llvm-project-17.0.6.src.tar.xz" \
    -DLLVM_SHA256="58a8818c60e6627064f312dbf46c02d9949956558340938b71cf731ad8bc0813"
cmake --build dependencies/build

cmake -G Ninja -B build "-DCMAKE_PREFIX_PATH=$PWD/dependencies/install"
cmake --build build
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

Rellic supports LLVM versions 16, 17, 18, 19, and 20. The compatibility layer in `include/rellic/BC/Compat.h` handles API differences automatically.

## Testing Multiple LLVM Versions

To test rellic with different LLVM versions:

```sh
for VER in 16 17 18 19 20; do
    # Clean previous builds
    rm -rf dependencies/build build-llvm$VER

    # Build dependencies
    cmake -G Ninja -S dependencies -B dependencies/build \
        -DUSE_EXTERNAL_LLVM=ON \
        -DCMAKE_PREFIX_PATH=$(llvm-config-$VER --cmakedir)/..
    cmake --build dependencies/build

    # Build rellic
    cmake -G Ninja -B build-llvm$VER \
        -DCMAKE_PREFIX_PATH="$(llvm-config-$VER --cmakedir)/..;$PWD/dependencies/install"
    cmake --build build-llvm$VER

    # Run tests
    ctest --test-dir build-llvm$VER
done
```

## Troubleshooting

### Ninja not found

```sh
sudo apt install ninja-build  # Linux
brew install ninja  # macOS
```

### LLVM not found on macOS

If Homebrew LLVM is installed but not detected:

```sh
export CMAKE_PREFIX_PATH=$(brew --prefix llvm@17)
cmake -G Ninja -S dependencies -B dependencies/build -DUSE_EXTERNAL_LLVM=ON
```

### Z3 build fails

Use an external Z3 installation:

```sh
cmake -G Ninja -S dependencies -B dependencies/build \
    -DUSE_EXTERNAL_LLVM=ON \
    -DUSE_EXTERNAL_Z3=ON
```
