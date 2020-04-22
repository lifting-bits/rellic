# Choose your LLVM version
ARG LLVM_VERSION=8.0

# Run-time dependencies go here
FROM ubuntu:18.04 as base
RUN apt-get update && \
    apt-get install -y \
     libomp5

# Build-time dependencies go here
FROM base as deps
RUN apt-get update && \
    apt-get install -y git \
     python \
     python3.7 \
     wget \
     curl \
     build-essential \
     libtinfo-dev \
     lsb-release \
     zlib1g-dev \
     unzip \
     libomp-dev

# Source code build
FROM deps as build
ARG LLVM_VERSION

WORKDIR /rellic-build
COPY ./ ./
# RUN ./scripts/build.sh --llvm-version $LLVM_VERSION
RUN ./scripts/build.sh --llvm-version $LLVM_VERSION --prefix /opt/rellic
RUN cd rellic-build && \
    make test && \
    make install


# Small installation image
FROM base as install
ARG LLVM_VERSION

COPY --from=build /opt/rellic /opt/rellic
COPY scripts/docker-decomp-entrypoint.sh /opt/rellic
ENV LLVM_VERSION=$LLVM_VERSION
ENTRYPOINT ["/opt/rellic/docker-decomp-entrypoint.sh"]
