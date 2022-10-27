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

If you are experiencing undocumented problems with Rellic then ask for help in the `#binary-lifting` channel of the [Empire Hacking Slack](https://empireslacking.herokuapp.com/).

## Supported Platforms

Rellic is supported on Linux platforms and has been tested on Ubuntu 20.04.

## Dependencies

Most of Rellic's dependencies can be provided by the [cxx-common](https://github.com/lifting-bits/cxx-common) repository. Trail of Bits hosts downloadable, pre-built versions of cxx-common, which makes it substantially easier to get up and running with Rellic. Nonetheless, the following table represents most of Rellic's dependencies.

| Name | Version |
| ---- | ------- |
| [Git](https://git-scm.com/) | Latest |
| [CMake](https://cmake.org/) | 3.21+ |
| [Google Flags](https://github.com/google/glog) | Latest |
| [Google Log](https://github.com/google/glog) | Latest |
| [LLVM](http://llvm.org/) | 14|
| [Clang](http://clang.llvm.org/) | 14|
| [Z3](https://github.com/Z3Prover/z3) | 4.7.1+ |

## Pre-made Docker Images

Pre-built Docker images are available on [Docker Hub](https://hub.docker.com/repository/docker/lifting-bits/rellic) and the Github Package Registry.

## Getting and Building the Code

### On Linux

First, update aptitude and get install the baseline dependencies.

```shell
sudo apt update
sudo apt upgrade

sudo apt install \
     git \
     python3 \
     wget \
     unzip \
     pixz \
     xz-utils \
     cmake \
     curl \
     build-essential \
     lsb-release \
     zlib1g-dev \
     libomp-dev \
     doctest-dev
```

If the distribution you're on doesn't include a recent release of CMake (3.21 or later), you'll need to install it. For Ubuntu, see here <https://apt.kitware.com/>.

The next step is to clone the Rellic repository.

```shell
git clone --recurse-submodules https://github.com/lifting-bits/rellic.git
```

Finally, we build and package Rellic. This script will create another directory, `rellic-build`, in the current working directory. All remaining dependencies needed by Rellic will be downloaded and placed in the parent directory alongside the repo checkout in `lifting-bits-downloads` (see the script's `-h` option for more details). This script also creates installable deb, rpm, and tgz packages.

```shell
cd rellic
./scripts/build.sh --llvm-version 14
# to install the deb package, then do:
sudo dpkg -i rellic-build/*.deb
```

To try out Rellic you can do the following, given a LLVM bitcode file of your choice.

```shell
# Create some sample bitcode or your own
clang-14 -emit-llvm -c ./tests/tools/decomp/issue_4.c -o ./tests/tools/decomp/issue_4.bc

./rellic-build/tools/rellic-decomp --input ./tests/tools/decomp/issue_4.bc --output /dev/stdout
```

### On macOS

Make sure to have the latest release of cxx-common for LLVM 14. Then, build with

```shell
cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DVCPKG_ROOT=/path/to/vcpkg \
  -DVCPKG_TARGET_TRIPLET=x64-osx-rel \
  -DRELLIC_ENABLE_TESTING=OFF \
  -DCMAKE_C_COMPILER=`which clang` \
  -DCMAKE_CXX_COMPILER=`which clang++` \
  /path/to/rellic

make -j8
```

### Docker image

The Docker image should provide an environment which can set-up, build, and run rellic. The Docker images are parameterized by Ubuntu verison, LLVM version, and architecture.

To build the docker image using LLVM 14 for Ubuntu 20.04 on amd64 you can run the following command:

```sh
ARCH=amd64; UBUNTU=20.04; LLVM=14; docker build . \
  -t rellic:llvm${LLVM}-ubuntu${UBUNTU}-${ARCH} \
  -f Dockerfile \
  --build-arg UBUNTU_VERSION=${UBUNTU} \
  --build-arg ARCH=${ARCH} \
  --build-arg LLVM_VERSION=${LLVM}
```

To run the decompiler, the entrypoint has already been set, but make sure the bitcode you are decompiling is the same LLVM version as the decompiler, and run:

```sh
# Get the bc file
clang-14 -emit-llvm -c ./tests/tools/decomp/issue_4.c -o ./tests/tools/decomp/issue_4.bc

# Decompile
docker run --rm -t -i \
  -v $(pwd):/test -w /test \
  -u $(id -u):$(id -g) \
  rellic:llvm14-ubuntu20.04-amd64 --input ./tests/tools/decomp/issue_4.bc --output /dev/stdout
```

To explain the above command more:

```sh
# Mount current directory and change working directory
-v $(pwd):/test -w /test
```

and

```sh
# Set the user to current user to ensure correct permissions
-u $(id -u):$(id -g) \
```

## Testing

We use several integration and unit tests to test rellic.

*Roundtrip tests* will take C code, build it to LLVM IR, and then translate that IR back to C. The test then sees if the resulting C can be built and if the translated code does (roughly) the same thing as the original. To run these, use:

```sh
cd rellic-build #or your rellic build directory
CTEST_OUTPUT_ON_FAILURE=1 cmake --build . --verbose --target test
```

*AnghaBench 1000* is a sample of 1000 files (x 4 architectures, so a total of 4000 tests) from the full million programs that come with AnghaBench. This test only checks whether the bitcode for these programs translates to C, not the prettiness or functionality of the resulting translation. To run this test, first install the required Python dependencies found in `scripts/requirements.txt` and then run:

```sh
scripts/test-angha-1k.sh --rellic-cmd <path_to_rellic_decompiler_exe>
```
