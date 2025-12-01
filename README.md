# Rellic

Rellic is an implementation of the [pattern-independent structuring](https://github.com/lifting-bits/rellic/blob/master/docs/NoMoreGotos.pdf) algorithm to produce a goto-free C output from LLVM bitcode.

The design philosophy behind the project is to provide a relatively small and easily hackable codebase with great interoperability with other LLVM and [Remill](https://github.com/lifting-bits/remill) projects.

## Examples

<table>
<thead>
  <td>Original program</td>
  <td>Compiled with <code>-emit-llvm -O0</code> and decompiled</td>
</thead>
<tbody>
<tr>
<td>

```c
int main() {
  for(int i = 0; i < 30; ++i) {
    if(i % 3 == 0 && i % 5 == 0) {
      printf("fizzbuzz\n");
    } else if(i % 3 == 0) {
      printf("fizz\n");
    } else if(i % 5 == 0) {
      printf("buzz\n");
    } else {
      printf("%d\n", i);
    }
  }
}
```

</td>
<td>

```c
int main() {
  unsigned int var0;
  unsigned int i;
  var0 = 0U;
  i = 0U;
  while ((int)i < 30) {
    if ((int)i % 3 != 0U || !((int)i % 5 == 0U || (int)i % 3 != 0U)) {
      if ((int)i % 3 != 0U) {
        if ((int)i % 5 != 0U) {
          printf("%d\n", i);
        } else {
          printf("buzz\n");
        }
      } else {
        printf("fizz\n");
      }
    } else {
      printf("fizzbuzz\n");
    }
    i = i + 1U;
  }
  return var0;
}
```

</td>
</tr>
<tr>
<td>

```c
int main() {
  int i = 0;
  start:
  i++;
  switch(i) {
    case 1: printf("%d\n", i); goto start; break;
    case 2: printf("%d\n", i); goto start; break;
    case 3: printf("%d\n", i); break;
  }
}
```

</td>
<td>

```c
int main() {
  unsigned int var0;
  unsigned int i;
  var0 = 0U;
  i = 0U;
  do {
    i = i + 1U;
    if (!(i != 3U && i != 2U && i != 1U))
      if (i == 3U) {
        printf("%d\n", i);
        break;
      } else if (i == 2U) {
        printf("%d\n", i);
      } else {
        printf("%d\n", i);
      }
  } while (!(i != 3U && i != 2U && i != 1U));
  return var0;
}
```

</td>
</tr>
<tr>
<td>

```c
int main() {
  int x = atoi("5");
  if(x > 10) {
    while(x < 20) {
      x = x + 1;
      printf("loop1 x: %d\n", x);
    }
  }
  while(x < 20) {
    x = x + 1;
    printf("loop2 x: %d\n", x);
  }
}
```

</td>
<td>

```c
int main() {
  unsigned int var0;
  unsigned int x;
  unsigned int call2;
  var0 = 0U;
  call2 = atoi("5");
  x = call2;
  if ((int)x > 10) {
    while ((int)x < 20) {
      x = x + 1U;
      printf("loop1 x: %d\n", x);
    }
  }
  if ((int)x <= 10 || (int)x >= 20) {
    while ((int)x < 20) {
      x = x + 1U;
      printf("loop2 x: %d\n", x);
    }
  }
  if ((int)x >= 20 && ((int)x <= 10 || (int)x >= 20)) {
    return var0;
  }
}
```

</td>
</tr>
</tbody>
</table>

## In the press

[C your data structures with `rellic-headergen`](https://blog.trailofbits.com/2022/01/19/c-your-data-structures-with-rellic-headergen/)

[Interactive decompilation with `rellic-xref`](https://blog.trailofbits.com/2022/05/17/interactive-decompilation-with-rellic-xref/)

[Magnifier: an experiment with interactive decompilation](https://blog.trailofbits.com/2022/08/25/magnifier-an-experiment-with-interactive-decompilation/)

## Build Status

|       | master |
| ----- | ------ |
| Linux | [![Build Status](https://github.com/lifting-bits/rellic/workflows/CI/badge.svg)](https://github.com/lifting-bits/rellic/actions?query=workflow%3ACI)|

## Getting Help

If you are experiencing undocumented problems with Rellic then ask for help in the `#binary-lifting` channel of the [Empire Hacking Slack](https://slack.empirehacking.nyc/).

## Supported Platforms

Rellic is supported on Linux platforms and has been tested on Ubuntu 22.04.

## Dependencies

Rellic uses a CMake-based superbuild system that automatically builds most dependencies from source. The following table lists the required dependencies:

| Name | Version |
| ---- | ------- |
| [Git](https://git-scm.com/) | Latest |
| [CMake](https://cmake.org/) | 3.21+ |
| [Ninja](https://ninja-build.org/) | Latest |
| [Python](https://www.python.org/) | 3.6+ |
| [Google Flags](https://github.com/gflags/gflags) | Latest (built by superbuild) |
| [Google Log](https://github.com/google/glog) | Latest (built by superbuild) |
| [LLVM](http://llvm.org/) | 20 |
| [Clang](http://clang.llvm.org/) | 20 |
| [Z3](https://github.com/Z3Prover/z3) | 4.13.4 (built by superbuild) |

**Note:** Rellic currently requires LLVM 20. You can use system-provided LLVM packages or build LLVM from source via the superbuild.

### External LLVM Requirements

If you use an external LLVM (via `-DUSE_EXTERNAL_LLVM=ON`), it must meet these requirements:

| Requirement | Details |
| ----------- | ------- |
| **Version** | LLVM 20 (other versions are not supported) |
| **Clang** | Must include Clang (`-DLLVM_ENABLE_PROJECTS="clang"`) |
| **RTTI** | Must be built with RTTI enabled (`-DLLVM_ENABLE_RTTI=ON`) |

System LLVM packages from [apt.llvm.org](https://apt.llvm.org/) (Linux) and [Homebrew](https://brew.sh/) (macOS) meet these requirements out of the box.

If building LLVM from source for use with rellic:

```shell
cmake -G Ninja -S llvm -B build \
  -DLLVM_ENABLE_PROJECTS="clang" \
  -DLLVM_ENABLE_RTTI=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/path/to/install
cmake --build build
cmake --install build
```

## Pre-made Docker Images

Pre-built Docker images are available on [Docker Hub](https://hub.docker.com/repository/docker/lifting-bits/rellic) and the Github Package Registry.

## Getting and Building the Code

### Quick Start (macOS or Linux)

```shell
# Step 1: Build dependencies (including LLVM 20, Z3, gflags, glog, etc.)
cmake -G Ninja -S dependencies -B dependencies/build
cmake --build dependencies/build

# Step 2: Build rellic
# On Linux:
cmake -G Ninja -B build -DCMAKE_PREFIX_PATH=$(pwd)/dependencies/install
# On macOS (need to specify sysroot for tests):
cmake -G Ninja -B build \
  -DCMAKE_PREFIX_PATH=$(pwd)/dependencies/install \
  -DCMAKE_OSX_SYSROOT=$(xcrun --show-sdk-path)

cmake --build build
```

**Note:** Step 1 builds LLVM from source and takes significant time and disk space (~2 hours, ~30GB).

### On Linux

Install baseline dependencies:

```shell
sudo apt update
sudo apt install -y git cmake ninja-build python3 build-essential
```

If your distribution doesn't include CMake 3.21 or later, install it from <https://apt.kitware.com/>.

Then follow the Quick Start above, or use system LLVM for faster builds:

#### Using System LLVM (Faster)

```shell
# Install LLVM 20
wget https://apt.llvm.org/llvm.sh && chmod +x llvm.sh && sudo ./llvm.sh 20
sudo apt install -y llvm-20-dev clang-20 libclang-20-dev

# Build dependencies (skip LLVM)
cmake -G Ninja -S dependencies -B dependencies/build -DUSE_EXTERNAL_LLVM=ON
cmake --build dependencies/build

# Build rellic
cmake -G Ninja -B build \
  -DCMAKE_PREFIX_PATH="/usr/lib/llvm-20;$(pwd)/dependencies/install"
cmake --build build
```

### On macOS

Install baseline dependencies:

```shell
brew install cmake ninja
```

Then follow the Quick Start above, or use Homebrew LLVM for faster builds:

#### Using Homebrew LLVM (Faster)

```shell
brew install llvm@20

# Build dependencies (skip LLVM)
cmake -G Ninja -S dependencies -B dependencies/build -DUSE_EXTERNAL_LLVM=ON
cmake --build dependencies/build

# Build rellic (specify sysroot for tests)
cmake -G Ninja -B build \
  -DCMAKE_PREFIX_PATH="$(brew --prefix llvm@20);$(pwd)/dependencies/install" \
  -DCMAKE_OSX_SYSROOT=$(xcrun --show-sdk-path)
cmake --build build
```

### Testing Rellic

```shell
# Run tests
CTEST_OUTPUT_ON_FAILURE=1 ctest --test-dir build

# Try it out (use clang from your LLVM installation)
clang -emit-llvm -c ./tests/tools/decomp/issue_4.c -o issue_4.bc
./build/tools/rellic-decomp-20 --input issue_4.bc --output /dev/stdout
```

### Docker image

The Dockerfile provides a complete build environment for Rellic with LLVM 20.

```sh
# Build the Docker image
docker build -t rellic:llvm20 .
```

Run the decompiler:

```sh
# Create sample bitcode
clang-20 -emit-llvm -c ./tests/tools/decomp/issue_4.c -o ./tests/tools/decomp/issue_4.bc

# Decompile using Docker
docker run --rm -t -i \
  -v $(pwd):/test -w /test \
  -u $(id -u):$(id -g) \
  rellic:llvm20 --input ./tests/tools/decomp/issue_4.bc --output /dev/stdout
```

Docker run flags explained:
- `-v $(pwd):/test -w /test` - Mount current directory and set as working directory
- `-u $(id -u):$(id -g)` - Run as current user to preserve file permissions

## Testing

Rellic includes comprehensive integration and unit tests.

*Roundtrip tests* compile C code to LLVM IR, decompile it back to C, and verify that the result compiles and behaves similarly to the original. To run all tests:

```sh
cd build  # or your rellic build directory
CTEST_OUTPUT_ON_FAILURE=1 ctest
```

*AnghaBench 1000* is a sample of 1000 files (x 4 architectures, so a total of 4000 tests) from the full million programs that come with AnghaBench. This test only checks whether the bitcode for these programs translates to C, not the prettiness or functionality of the resulting translation. To run this test, first install the required Python dependencies found in `scripts/requirements.txt` and then run:

```sh
scripts/test-angha-1k.sh --rellic-cmd <path_to_rellic_decompiler_exe>
```

## Citing Rellic

Please use the following BibTeX snippet to cite Rellic:

```tex
@online{rellic,
  title={Rellic},
  author={Surovič, Marek and Bertolaccini, Francesco},
  organization={Trail of Bits},
  url={https://github.com/lifting-bits/rellic}
}
```
