# Rellic

Rellic is an implementation of the [pattern-independent structuring](https://github.com/trailofbits/rellic/blob/master/docs/NoMoreGotos.pdf) algorithm to produce a goto-free C output from LLVM bitcode.

The design philosophy behind the project is to provide a relatively small and easily hackable codebase with great interoperability with other LLVM and [Remill](https://github.com/trailofbits/remill) projects.

## Build Status

|       | master |
| ----- | ------ |
| Linux | [![Build Status](https://travis-ci.org/lifting-bits/rellic.svg?branch=master)](https://travis-ci.org/trailofbits/rellic)|

## Getting Help

If you are experiencing undocumented problems with Rellic then ask for help in the `#binary-lifting` channel of the [Empire Hacking Slack](https://empireslacking.herokuapp.com/).

## Supported Platforms

Rellic is supported on Linux platforms and has been tested on Ubuntu 16.04 and 18.04.

## Dependencies

Most of Rellic's dependencies can be provided by the [cxx-common](https://github.com/trailofbits/cxx-common) repository. Trail of Bits hosts downloadable, pre-built versions of cxx-common, which makes it substantially easier to get up and running with Rellic. Nonetheless, the following table represents most of Rellic's dependencies.

| Name | Version | 
| ---- | ------- |
| [Git](https://git-scm.com/) | Latest |
| [CMake](https://cmake.org/) | 3.2+ |
| [Google Flags](https://github.com/google/glog) | Latest |
| [Google Log](https://github.com/google/glog) | Latest |
| [LLVM](http://llvm.org/) | 4.0+|
| [Clang](http://clang.llvm.org/) | 4.0+|
| [Z3](https://github.com/Z3Prover/z3) | 4.7.1+ |

## Getting and Building the Code

### On Linux

First, update aptitude and get install the baseline dependencies.

```shell
sudo apt-get update
sudo apt-get upgrade

sudo apt-get install \
     git \
     python3.7 \
     wget \
     unzip \
     curl \
     build-essential \
     libtinfo-dev \
     lsb-release \
     zlib1g-dev \
     libomp-dev

# Ubuntu 14.04, 16.04
sudo apt-get install realpath
```

Note: The test script requires Python 3.7 and higher. If you're using Ubuntu 18.04 or older check the [Dockerfile](./Dockerfile) on how to install Python 3.7.

The next step is to clone the Rellic repository.

```shell
git clone https://github.com/trailofbits/rellic.git
```

Finally, we build Rellic. This script will create another directory, `rellic-build`, in the current working directory. All remaining dependencies needed by Rellic will be built in the `rellic-build` directory. Rellic will also download Z3 in an appropriate version if it's not installed in the system.

```shell
./rellic/scripts/build.sh
```

To try out Rellic you can do the following, given a LLVM bitcode file of your choice.

```shell
./rellic-build/rellic-decomp --input mybitcode.bc --output /dev/stdout
```

### Docker image

The Docker image should provide an environment which can set-up, build, and run rellic.

To build the docker image:
```sh
docker build . -t rellic:llvm800-ubuntu18.04-amd64 -f Dockerfile --build-arg UBUNTU_VERSION=18.04 --build-arg ARCH=amd64 --build-arg LLVM_VERSION=800
```

To run the decompiler, the entrypoint has already been set, but make sure the bitcode you are decompiling is the same LLVM version as the decompiler, and run:

```sh
# Get the bc file
clang-8 -emit-llvm -c ./tests/tools/decomp/issue_4.c -o ./tests/tools/decomp/issue_4.bc

# Decompile
docker run --rm -t -i \
  -v $(pwd):/test -w /test \
  -u $(id -u):$(id -g) \
  rellic:llvm800-ubuntu18.04-amd64 --input ./tests/tools/decomp/issue_4.bc --output /dev/stdout
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
