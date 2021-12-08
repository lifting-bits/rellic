#!/bin/sh

# Needed to process multiple arguments to docker image

V=""
case ${LLVM_VERSION} in
  llvm12*)
    V=12.0
  ;;
  *)
    echo "Unknown LLVM version: ${LLVM_VERSION}"
    exit 1
  ;;
esac

/opt/trailofbits/bin/rellic-decomp-${V} "$@"
