# Rellic

Rellic is an implementation of the [pattern-independent structuring](https://github.com/lifting-bits/rellic/blob/master/docs/NoMoreGotos.pdf) algorithm to produce a goto-free C output from LLVM bitcode.

The design philosophy behind the project is to provide a relatively small and easily hackable codebase with great interoperability with other LLVM and [Remill](https://github.com/lifting-bits/remill) projects.

## Build Status

|       | master |
| ----- | ------ |
| Linux | [![Build Status](https://github.com/lifting-bits/rellic/workflows/CI/badge.svg)](https://github.com/lifting-bits/rellic/actions?query=workflow%3ACI)|

## Getting Help

If you are experiencing undocumented problems with Rellic then ask for help in the `#binary-lifting` channel of the [Empire Hacking Slack](https://empireslacking.herokuapp.com/).

## Supported Platforms

Rellic is supported on Linux platforms and has been tested on Ubuntu 16.04 and 18.04.

## Dependencies

Most of Rellic's dependencies can be provided by the [cxx-common](https://github.com/lifting-bits/cxx-common) repository. Trail of Bits hosts downloadable, pre-built versions of cxx-common, which makes it substantially easier to get up and running with Rellic. Nonetheless, the following table represents most of Rellic's dependencies.

| Name | Version |
| ---- | ------- |
| [Git](https://git-scm.com/) | Latest |
| [CMake](https://cmake.org/) | 3.14+ |
| [Google Flags](https://github.com/google/glog) | Latest |
| [Google Log](https://github.com/google/glog) | Latest |
| [LLVM](http://llvm.org/) | 12|
| [Clang](http://clang.llvm.org/) | 12|
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

If the distribution you're on doesn't include a recent release of CMake (3.14 or later), you'll need to install it. For Ubuntu, see here https://apt.kitware.com/.

The next step is to clone the Rellic repository.

```shell
git clone --recurse-submodules https://github.com/lifting-bits/rellic.git
```

Finally, we build and package Rellic. This script will create another directory, `rellic-build`, in the current working directory. All remaining dependencies needed by Rellic will be downloaded and placed in the parent directory alongside the repo checkout in `lifting-bits-downloads` (see the script's `-h` option for more details). This script also creates installable deb, rpm, and tgz packages.

```shell
cd rellic
./scripts/build.sh --llvm-version 12
# to install the deb package, then do:
sudo dpkg -i rellic-build/*.deb
```

To try out Rellic you can do the following, given a LLVM bitcode file of your choice.

```shell
# Create some sample bitcode or your own
clang-12 -emit-llvm -c ./tests/tools/decomp/issue_4.c -o ./tests/tools/decomp/issue_4.bc

./rellic-build/tools/rellic-decomp --input ./tests/tools/decomp/issue_4.bc --output /dev/stdout
```

### On macOS

Make sure to have the latest release of cxx-common for LLVM 12. Then, build with

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

To build the docker image using LLVM 12 for Ubuntu 18.04 on amd64 you can run the following command:
```sh
ARCH=amd64; UBUNTU=18.04; LLVM=12; docker build . \
  -t rellic:llvm${LLVM}-ubuntu${UBUNTU}-${ARCH} \
  -f Dockerfile \
  --build-arg UBUNTU_VERSION=${UBUNTU} \
  --build-arg ARCH=${ARCH} \
  --build-arg LLVM_VERSION=${LLVM}
```

To run the decompiler, the entrypoint has already been set, but make sure the bitcode you are decompiling is the same LLVM version as the decompiler, and run:

```sh
# Get the bc file
clang-12 -emit-llvm -c ./tests/tools/decomp/issue_4.c -o ./tests/tools/decomp/issue_4.bc

# Decompile
docker run --rm -t -i \
  -v $(pwd):/test -w /test \
  -u $(id -u):$(id -g) \
  rellic:llvm12-ubuntu18.04-amd64 --input ./tests/tools/decomp/issue_4.bc --output /dev/stdout
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
