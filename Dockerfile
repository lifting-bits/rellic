# Choose your LLVM version
ARG LLVM_VERSION=11
ARG ARCH=amd64
ARG UBUNTU_VERSION=18.04
ARG DISTRO_BASE=ubuntu${UBUNTU_VERSION}
ARG BUILD_BASE=ubuntu:${UBUNTU_VERSION}
ARG LIBRARIES=/opt/trailofbits


# Run-time dependencies go here
FROM ${BUILD_BASE} as base

# Build-time dependencies go here
# See here for full list of those dependencies
# https://github.com/trailofbits/cxx-common/blob/master/docker/Dockerfile.ubuntu.vcpkg
FROM trailofbits/cxx-common-vcpkg-builder-ubuntu:${UBUNTU_VERSION} as deps
ARG UBUNTU_VERSION
ARG ARCH
ARG LLVM_VERSION
ARG LIBRARIES

RUN apt-get update && \
    apt-get install -qqy python3 python3-pip libc6-dev wget liblzma-dev zlib1g-dev curl git build-essential ninja-build libselinux1-dev libbsd-dev ccache pixz xz-utils make rpm && \
    if [ "$(uname -m)" = "x86_64" ]; then dpkg --add-architecture i386 && apt-get update && apt-get install -qqy gcc-multilib g++-multilib zip zlib1g-dev:i386; fi && \
    rm -rf /var/lib/apt/lists/*

# Source code build
FROM deps as build
ARG LLVM_VERSION
ARG LIBRARIES
ENV TRAILOFBITS_LIBRARIES="${LIBRARIES}"
ENV PATH="${LIBRARIES}/llvm/bin/:${LIBRARIES}/cmake/bin:${PATH}"

WORKDIR /rellic-build
COPY ./ ./
RUN ./scripts/build.sh \
  --llvm-version ${LLVM_VERSION} \
  --prefix /opt/trailofbits \
  --extra-cmake-args "-DCMAKE_BUILD_TYPE=Release"

RUN cd rellic-build && \
    CTEST_OUTPUT_ON_FAILURE=1 cmake --build . --verbose --target test && \
    cmake --build . --target install

# Small installation image
FROM base as install
ARG LLVM_VERSION

COPY --from=build /opt/trailofbits /opt/trailofbits
COPY scripts/docker-decomp-entrypoint.sh /opt/trailofbits/rellic
ENV LLVM_VERSION=llvm${LLVM_VERSION}
ENTRYPOINT ["/opt/trailofbits/docker-decomp-entrypoint.sh"]
