#!/bin/sh

# Needed to process multiple arguments to docker image

V=""
case ${LLVM_VERSION} in
  llvm35*)
    V=3.5
  ;;
  llvm36*)
    V=3.6
  ;;
  llvm37*)
    V=3.7
  ;;
  llvm38*)
    V=3.8
  ;;
  llvm39*)
    V=3.9
  ;;
  # There is an llvm401 that we treat as 4.0
  llvm4*)
    V=4.0
  ;;
  llvm5*)
    V=5.0
  ;;
  llvm6*)
    V=6.0
  ;;
  llvm7*)
    V=7.0
  ;;
  llvm8*)
    V=8.0
  ;;
  llvm9*)
    V=9.0
  ;;
  llvm10*)
    V=10.0
  ;;
  llvm11*)
    V=11.0
  ;;
  llvm12*)
    V=12.0
  ;;
  *)
    echo "Unknown LLVM version: ${LLVM_VERSION}"
    exit 1
  ;;
esac

/opt/trailofbits/bin/rellic-decomp-${V} "$@"
