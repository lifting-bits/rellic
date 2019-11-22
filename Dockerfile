# Choose your LLVM version
ARG LLVM_VERSION=8.0


FROM ubuntu:18.04 as DEPS
RUN apt-get update && \
    apt-get install -y git \
     python2.7 \
     python3.7 \
     wget \
     curl \
     build-essential \
     libtinfo-dev \
     lsb-release \
     zlib1g-dev \
     unzip \
     libomp-dev


FROM DEPS as BUILD
ARG LLVM_VERSION

WORKDIR /rellic-build
# Copy everything but the entrypoint
COPY ./ ./
RUN ./scripts/build.sh --llvm-version $LLVM_VERSION
RUN cd rellic-build && \
    make test


FROM DEPS as INSTALL
ARG LLVM_VERSION

COPY --from=BUILD /rellic-build/rellic-build /opt/rellic
COPY scripts/docker-decomp-entrypoint.sh /opt/rellic
ENV LLVM_VERSION=$LLVM_VERSION
ENTRYPOINT ["/opt/rellic/docker-decomp-entrypoint.sh"]
