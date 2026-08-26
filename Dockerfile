FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    gdb \
    git \
    curl \
    ca-certificates \
    pkg-config \
    liburing-dev \
    iproute2 \
    linux-tools-generic \
    linux-tools-common \
    numactl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
