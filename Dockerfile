# Multi-version LLVM support for Rellic
ARG LLVM_VERSION=20
ARG UBUNTU_VERSION=22.04

FROM ubuntu:${UBUNTU_VERSION} as build
ARG LLVM_VERSION
ARG UBUNTU_VERSION

# Install system dependencies
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      wget ca-certificates gnupg lsb-release software-properties-common \
      git cmake ninja-build python3 python-is-python3 \
      build-essential && \
    rm -rf /var/lib/apt/lists/*

# Install LLVM from apt.llvm.org
RUN wget https://apt.llvm.org/llvm.sh && \
    chmod +x llvm.sh && \
    ./llvm.sh ${LLVM_VERSION} && \
    apt-get install -y --no-install-recommends \
      llvm-${LLVM_VERSION}-dev \
      clang-${LLVM_VERSION} \
      libclang-${LLVM_VERSION}-dev && \
    rm -rf /var/lib/apt/lists/*

# Set compiler environment
ENV CC=clang-${LLVM_VERSION}
ENV CXX=clang++-${LLVM_VERSION}
ENV LLVM_DIR=/usr/lib/llvm-${LLVM_VERSION}/lib/cmake/llvm

# Build dependencies
WORKDIR /build
COPY dependencies/ /build/dependencies/
RUN cmake -G Ninja -S dependencies -B dependencies/build \
      -DUSE_EXTERNAL_LLVM=ON \
      -DCMAKE_PREFIX_PATH="${LLVM_DIR}/.." && \
    cmake --build dependencies/build

# Build rellic
COPY . /build/rellic
WORKDIR /build/rellic
RUN cmake -G Ninja -B build \
      -DCMAKE_PREFIX_PATH="${LLVM_DIR}/..;/build/dependencies/install" \
      -DCMAKE_INSTALL_PREFIX="/opt/trailofbits" \
      -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build && \
    cmake --install build

# Create minimal runtime image
FROM ubuntu:${UBUNTU_VERSION} as install
ARG LLVM_VERSION

# Install only runtime dependencies
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      llvm-${LLVM_VERSION} \
      libz3-4 && \
    rm -rf /var/lib/apt/lists/*

COPY --from=build /opt/trailofbits /opt/trailofbits
COPY scripts/docker-decomp-entrypoint.sh /opt/trailofbits/

ENV LLVM_VERSION=llvm${LLVM_VERSION}
ENV PATH="/opt/trailofbits/bin:${PATH}"

ENTRYPOINT ["/opt/trailofbits/docker-decomp-entrypoint.sh"]
