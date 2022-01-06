#!/bin/bash

#These are filled in by the CI system
export RELLIC_BRANCH=__RELLIC_BRANCH__
export RUN_SIZE=__RUN_SIZE__

export LLVM_VERSION=12
export CC=clang-12 CXX=clang++-12


echo "Saving output to $(pwd)/build.log"

{
    apt-get update
    apt-get install -yqq s3cmd pixz curl git python3 python3-pip xz-utils cmake ninja-build clang-${LLVM_VERSION}
    python3 -m pip install requests
    #install new cmake
    curl -LO https://github.com/Kitware/CMake/releases/download/v3.22.1/cmake-3.22.1-linux-x86_64.sh
    sh ./cmake-3.22.1-linux-x86_64.sh --skip-license --prefix=/usr
} &>> build.log

{
    git clone --recursive --shallow-submodules --depth=1 -b ${RELLIC_BRANCH}  https://github.com/lifting-bits/rellic rellic
    # CI Branch is defined by the CI system
    git clone --recursive --shallow-submodules --depth=1 -b ${CI_BRANCH} https://github.com/lifting-bits/lifting-tools-ci ci
} &>> build.log

{
    pushd rellic
    # build us a rellic
    scripts/build.sh \
        --install \
        --llvm-version ${LLVM_VERSION} \
        --extra-cmake-args "-DCMAKE_BUILD_TYPE=Release"
    if [[ "$?" != "0" ]]
    then
        exit 1
    fi
    popd
} &>> build.log

{
    pushd ci
    # Install extra requirements if needed
    if [[ -f requirements.txt ]]
    then
        python3 -m pip install -r requirements.txt
    fi

    mkdir -p $(pwd)/decompiled
    mkdir -p $(pwd)/recompiled

    # default to 1k
    if [[ "${RUN_SIZE,,}" = "__run_size__" ]]
    then
    RUN_SIZE=1k
    fi

    datasets/fetch_anghabench.sh --clang ${LLVM_VERSION} --bitcode --run-size ${RUN_SIZE}

    for i in *.tar.xz
    do
        tar -xJf $i
    done

    # Run the benchmark
    tool_run_scripts/rellic.py \
        --run-name "[${RUN_NAME}] [size: ${RUN_SIZE}] [rellic: ${RELLIC_BRANCH}]" \
        --rellic rellic-decomp-${LLVM_VERSION}.0 \
        --input-dir $(pwd)/bitcode \
        --output-dir $(pwd)/decompiled \
        --slack-notify

    # Try to recompile our decompiled code
    tool_run_scripts/recompile.py \
        --run-name "[${RUN_NAME}] [size: ${RUN_SIZE}] [recompile]" \
        --clang clang-${LLVM_VERSION} \
        --input-dir $(pwd)/decompiled \
        --output-dir $(pwd)/recompiled \
        --slack-notify

    # AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY passed in from original invocation environment
    if [[ "${AWS_ACCESS_KEY_ID,,}" != "" ]]
    then
      datenow=$(date +'%F-%H-%M')
      url_base="https://tob-amp-ci-results.nyc3.digitaloceanspaces.com"
      tar -Ipixz -cf rellic-ci-${datenow}.tar.xz decompiled
      tar -Ipixz -cf recompile-ci-${datenow}.tar.xz recompiled

      s3cmd -c /dev/null \
        '--host-bucket=%(bucket)s.nyc3.digitaloceanspaces.com' \
        --acl-public \
        put \
        rellic-ci-${datenow}.tar.xz \
        s3://tob-amp-ci-results/rellic/

      tool_run_scripts/slack.py \
        --msg "Uploaded rellic decompilation results to ${url_base}/rellic/rellic-ci-${datenow}.tar.xz"
          

      s3cmd -c /dev/null \
        '--host-bucket=%(bucket)s.nyc3.digitaloceanspaces.com' \
        --acl-public \
        put \
        recompile-ci-${datenow}.tar.xz \
        s3://tob-amp-ci-results/recompile/

      tool_run_scripts/slack.py \
        --msg "Uploaded recompilation results to ${url_base}/recompile/recompile-ci-${datenow}.tar.xz"
    fi

    # exit hook called here
} &>> build.log

exit 0
