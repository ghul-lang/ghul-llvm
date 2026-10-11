# The build environment. LLVM's release libraries are built against glibc
# 2.34, and building here, on glibc 2.35, keeps ghul-llvm's own requirement
# at the oldest glibc those libraries allow: building on a newer distribution
# would raise it to whatever that one has.
FROM ubuntu:22.04

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        binutils \
        ca-certificates \
        cmake \
        curl \
        file \
        g++ \
        make \
        ninja-build \
        libzstd-dev \
        zlib1g-dev \
        zstd \
    && rm -rf /var/lib/apt/lists/*
