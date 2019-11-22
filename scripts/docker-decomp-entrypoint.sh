#!/bin/sh

# Needed to process multiple arguments to docker image
/opt/rellic/tools/rellic-decomp-${LLVM_VERSION} "$@"
