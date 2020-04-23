# Choose your LLVM version
ARG LLVM_VERSION=8.0

# Run-time dependencies go here
FROM ubuntu:18.04 as base
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
     libgomp1 && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Build-time dependencies go here
FROM base as deps
RUN apt-get update && \
    apt-get install -y \
     git \
     python \
     python3.7 \
     wget \
     curl \
     gcc-multilib \
     g++-multilib \
     build-essential \
     libtinfo-dev \
     lsb-release \
     zlib1g-dev \
     unzip \
     libomp-dev && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Source code build
FROM deps as build
ARG LLVM_VERSION

WORKDIR /rellic-build
COPY ./ ./
# LLVM 7.0 doesn't work without `--use-host-compiler`
RUN ./scripts/build.sh --llvm-version $LLVM_VERSION --prefix /opt/rellic --use-host-compiler --extra-cmake-args "-DCMAKE_BUILD_TYPE=Release"
RUN cd rellic-build && \
    make test ARGS="-V" && \
    make install


# Small installation image
FROM base as install
ARG LLVM_VERSION

COPY --from=build /opt/rellic /opt/rellic
COPY scripts/docker-decomp-entrypoint.sh /opt/rellic
ENV LLVM_VERSION=$LLVM_VERSION
ENTRYPOINT ["/opt/rellic/docker-decomp-entrypoint.sh"]
