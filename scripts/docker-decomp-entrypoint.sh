#!/bin/sh

# Needed to process multiple arguments to docker image
/opt/trailofbits/rellic/bin/rellic-decomp-${LLVM_VERSION} "$@"
