#!/bin/sh

# Needed to process multiple arguments to docker image
/opt/rellic/bin/rellic-decomp-${LLVM_VERSION} "$@"
