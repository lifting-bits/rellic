#!/bin/bash

#These are filled in by the CI system
export RELLIC_BRANCH=__RELLIC_BRANCH__
export RUN_SIZE=__RUN_SIZE__

export LLVM_VERSION=11
export CC=clang-11 CXX=clang++-11

apt-get update
apt-get install -yqq curl git python3 python3-pip xz-utils cmake ninja-build clang-11
python3 -m pip install requests

git clone --depth=1 -b ${RELLIC_BRANCH}  https://github.com/lifting-bits/rellic rellic
# CI Branch is defined by the CI system
git clone --depth=1 -b ${CI_BRANCH} https://github.com/lifting-bits/lifting-tools-ci ci

pushd rellic
# build us a rellic
scripts/build.sh \
    --install \
    --llvm-version ${LLVM_VERSION} \
    --extra-cmake-args "-DCMAKE_BUILD_TYPE=Release"
popd

pushd ci

# Install extra requirements if needed
if [[ -f requirements.txt ]]
then
    python3 -m pip install -r requirements.txt
fi

mkdir -p $(pwd)/output

# default to 1k
if [[ "${RUN_SIZE,,}" = "__run_size__" ]]
then
   RUN_SIZE=1k
fi

datasets/fetch_anghabench.sh --bitcode --run-size ${RUN_SIZE}

for i in *.tar.xz
do
    tar -xJf $i
done

# Run the benchmark
tool_run_scripts/rellic.py \
    --run-name "[${RUN_NAME}] [size: ${RUN_SIZE}] [rellic: ${RELLIC_BRANCH}]" \
    --rellic rellic-decomp-${LLVM_VERSION}.0 \
    --input-dir $(pwd)/bitcode \
    --output-dir $(pwd)/output \
    --slack-notify

# exit hook called here
exit 0