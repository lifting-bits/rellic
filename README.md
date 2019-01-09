# Rellic

Rellic is an implementation of the [pattern-independent structuring](https://github.com/trailofbits/rellic/blob/master/docs/NoMoreGotos.pdf) algorithm to produce a goto-free C output from LLVM bitcode.

The design philosophy behind the project is to provide a relatively small and easily hackable codebase with great interoperability with other LLVM and [Remill](https://github.com/trailofbits/remill) projects.

## Build Status

|       | master |
| ----- | ------ |
| Linux |        |

## Getting Help

If you are experiencing undocumented problems with Rellic then ask for help in the `#binary-lifting` channel of the [Empire Hacking Slack](https://empireslacking.herokuapp.com/).

## Supported Platforms

Rellic is supported on Linux platforms and has been tested on Ubuntu 18.04.

## Dependencies

Most of Rellic's dependencies can be provided by the [cxx-common](https://github.com/trailofbits/cxx-common) repository. Trail of Bits hosts downloadable, pre-built versions of cxx-common, which makes it substantially easier to get up and running with Rellic. Nonetheless, the following table represents most of Rellic's dependencies.

| Name | Version | 
| ---- | ------- |
| [Git](https://git-scm.com/) | Latest |
| [CMake](https://cmake.org/) | 3.2+ |
| [Google Flags](https://github.com/google/glog) | Latest |
| [Google Log](https://github.com/google/glog) | Latest |
| [LLVM](http://llvm.org/) | 3.5+|
| [Clang](http://clang.llvm.org/) | 3.5+|
| [Z3](https://github.com/Z3Prover/z3) | 4.7.1 |

## Getting and Building the Code

### On Linux

First, update aptitude and get install the baseline dependencies.

TODO(msurovic): z3 installation

```shell
sudo apt-get update
sudo apt-get upgrade

sudo apt-get install \
     git \
     python2.7 \
     wget \
     curl \
     build-essential \
     libtinfo-dev \
     lsb-release \
     zlib1g-dev

# Ubuntu 14.04, 16.04
sudo apt-get install realpath
```

The next step is to clone the Rellic repository.

```shell
git clone https://github.com/trailofbits/rellic.git
```

Finally, we build Rellic. This script will create another directory, `rellic-build`, in the current working directory. All remaining dependencies needed by Rellic will be built in the `rellic-build` directory.

```shell
cd ../../
./rellic/scripts/build.sh
```

To try out Rellic you can do the following, given a LLVM bitcode file of your choice.

```shell
./rellic-build/rellic-decomp --input mybitcode.bc --output /dev/stdout
```