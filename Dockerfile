# Choose your LLVM version (only _some_ versions are supported)
ARG LLVM_VERSION=16
ARG UBUNTU_VERSION=22.04
ARG DISTRO_BASE=ubuntu${UBUNTU_VERSION}
ARG BUILD_BASE=ubuntu:${UBUNTU_VERSION}
ARG LIBRARIES=/opt/trailofbits


# Run-time dependencies go here
FROM ${BUILD_BASE} as base

# Build-time dependencies go here
# See here for full list of those dependencies
# https://github.com/lifting-bits/cxx-common/blob/master/docker/Dockerfile.ubuntu.vcpkg
FROM ghcr.io/lifting-bits/cxx-common/vcpkg-builder-ubuntu-v2:${UBUNTU_VERSION} as deps
ARG UBUNTU_VERSION
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
ENV CC=clang
ENV CXX=clang++

WORKDIR /rellic
COPY ./ ./
RUN ./scripts/build.sh \
  --llvm-version ${LLVM_VERSION} \
  --prefix /opt/trailofbits \
  --extra-cmake-args "-DCMAKE_BUILD_TYPE=Release" \
  --install

# Small installation image
FROM base as install
ARG LLVM_VERSION

COPY --from=build /opt/trailofbits /opt/trailofbits
COPY scripts/docker-decomp-entrypoint.sh /opt/trailofbits
ENV LLVM_VERSION=llvm${LLVM_VERSION}
ENTRYPOINT ["/opt/trailofbits/docker-decomp-entrypoint.sh"]
