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
| [LLVM](http://llvm.org/) | 16, 17, 18, 19, or 20 |
| [Clang](http://clang.llvm.org/) | 16, 17, 18, 19, or 20 |
| [Z3](https://github.com/Z3Prover/z3) | 4.13.0 (built by superbuild) |

**Note:** Rellic supports LLVM versions 16 through 20. You can use system-provided LLVM packages or build LLVM from source via the superbuild.

## Pre-made Docker Images

Pre-built Docker images are available on [Docker Hub](https://hub.docker.com/repository/docker/lifting-bits/rellic) and the Github Package Registry.

## Getting and Building the Code

### On Linux

First, install the baseline dependencies:

```shell
sudo apt update
sudo apt install -y \
     git \
     cmake \
     ninja-build \
     python3 \
     build-essential \
     wget \
     ca-certificates \
     gnupg \
     lsb-release \
     software-properties-common
```

If your distribution doesn't include CMake 3.21 or later, install it from <https://apt.kitware.com/>.

#### Option 1: Using System LLVM (Recommended)

Install LLVM from the official LLVM apt repository:

```shell
# Install LLVM 20 (or choose 16, 17, 18, 19)
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 20
sudo apt install -y llvm-20-dev clang-20 libclang-20-dev
```

Clone and build Rellic:

```shell
git clone https://github.com/lifting-bits/rellic.git
cd rellic

# Build dependencies (gflags, glog, Z3, etc.)
cmake -G Ninja -S dependencies -B dependencies/build \
  -DUSE_EXTERNAL_LLVM=ON \
  -DCMAKE_PREFIX_PATH="/usr/lib/llvm-20/lib/cmake/llvm/.."
cmake --build dependencies/build

# Build rellic
cmake -G Ninja -B build \
  -DCMAKE_PREFIX_PATH="/usr/lib/llvm-20/lib/cmake/llvm/..;$PWD/dependencies/install" \
  -DCMAKE_INSTALL_PREFIX="$PWD/install" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build
```

#### Option 2: Building LLVM from Source

If you prefer to build LLVM from source, omit the `-DUSE_EXTERNAL_LLVM=ON` flag:

```shell
cmake -G Ninja -S dependencies -B dependencies/build
cmake --build dependencies/build  # This will take a while!

cmake -G Ninja -B build \
  -DCMAKE_PREFIX_PATH="$PWD/dependencies/install" \
  -DCMAKE_INSTALL_PREFIX="$PWD/install" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build
```

#### Testing Rellic

```shell
# Create sample bitcode
clang-20 -emit-llvm -c ./tests/tools/decomp/issue_4.c -o ./tests/tools/decomp/issue_4.bc

# Decompile
./install/bin/rellic-decomp-20 --input ./tests/tools/decomp/issue_4.bc --output /dev/stdout

# Run tests
CTEST_OUTPUT_ON_FAILURE=1 ctest --test-dir build
```

### On macOS

First, install dependencies using Homebrew:

```shell
brew install cmake ninja llvm@20
```

Clone and build Rellic:

```shell
git clone https://github.com/lifting-bits/rellic.git
cd rellic

# Build dependencies (gflags, glog, Z3, etc.)
cmake -G Ninja -S dependencies -B dependencies/build \
  -DUSE_EXTERNAL_LLVM=ON \
  -DCMAKE_PREFIX_PATH="$(brew --prefix llvm@20)/lib/cmake/llvm/.."
cmake --build dependencies/build

# Build rellic
cmake -G Ninja -B build \
  -DCMAKE_PREFIX_PATH="$(brew --prefix llvm@20)/lib/cmake/llvm/..;$PWD/dependencies/install" \
  -DCMAKE_INSTALL_PREFIX="$PWD/install" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build
```

**Note:** You can use any LLVM version from 16 to 20. Just replace `llvm@20` with your preferred version (e.g., `llvm@18`).

#### Testing Rellic on macOS

```shell
# Create sample bitcode
$(brew --prefix llvm@20)/bin/clang -emit-llvm -c ./tests/tools/decomp/issue_4.c -o ./tests/tools/decomp/issue_4.bc

# Decompile
./install/bin/rellic-decomp-20 --input ./tests/tools/decomp/issue_4.bc --output /dev/stdout

# Run tests
CTEST_OUTPUT_ON_FAILURE=1 ctest --test-dir build
```

### Docker image

The Dockerfile provides a complete build environment for Rellic. Docker images are parameterized by Ubuntu version and LLVM version.

Build a Docker image with your preferred LLVM version (16-20):

```sh
# Build with LLVM 20 (default)
docker build -t rellic:llvm20 .

# Or specify a different LLVM version
docker build -t rellic:llvm18 --build-arg LLVM_VERSION=18 .

# Customize Ubuntu version if needed
docker build -t rellic:llvm20-ubuntu22 \
  --build-arg LLVM_VERSION=20 \
  --build-arg UBUNTU_VERSION=22.04 .
```

Run the decompiler (ensure your bitcode matches the LLVM version):

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
