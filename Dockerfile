# Choose your LLVM version
ARG LLVM_VERSION=800
ARG ARCH=amd64
ARG UBUNTU_VERSION=18.04
ARG DISTRO_BASE=ubuntu${UBUNTU_VERSION}
ARG BUILD_BASE=ubuntu:${UBUNTU_VERSION}
ARG LIBRARIES=/opt/trailofbits/libraries


# Run-time dependencies go here
FROM ${BUILD_BASE} as base

# Build-time dependencies go here
FROM trailofbits/cxx-common:llvm${LLVM_VERSION}-${DISTRO_BASE}-${ARCH} as deps
ARG LLVM_VERSION
ARG LIBRARIES

RUN apt-get update && \
    apt-get install -y \
     build-essential \
     git \
     zlib1g-dev \
     libtinfo-dev \
     python3 \
     ninja-build && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Source code build
FROM deps as build
ARG LLVM_VERSION
ARG LIBRARIES
ENV TRAILOFBITS_LIBRARIES="${LIBRARIES}"
ENV PATH="${LIBRARIES}/llvm/bin/:${LIBRARIES}/cmake/bin:${PATH}"
ENV CC=clang CXX=clang++

WORKDIR /rellic-build
COPY ./ ./
# LLVM 7.0 doesn't work without `--use-host-compiler`
RUN ./scripts/build.sh \
  --llvm-version llvm${LLVM_VERSION} \
  --prefix /opt/trailofbits/rellic \
  --use-host-compiler \
  --extra-cmake-args "-DCMAKE_BUILD_TYPE=Release" \
  --build-only
RUN cd rellic-build && \
    CTEST_OUTPUT_ON_FAILURE=1 "${LIBRARIES}/cmake/bin/cmake" --build . --verbose --target test && \
    "${LIBRARIES}/cmake/bin/cmake" --build . --target install

# Small installation image
FROM base as install
ARG LLVM_VERSION

RUN mkdir -p /opt/trailofbits
COPY --from=build /opt/trailofbits/rellic /opt/trailofbits/rellic
COPY scripts/docker-decomp-entrypoint.sh /opt/trailofbits/rellic
ENV LLVM_VERSION=llvm${LLVM_VERSION}
ENTRYPOINT ["/opt/trailofbits/rellic/docker-decomp-entrypoint.sh"]
