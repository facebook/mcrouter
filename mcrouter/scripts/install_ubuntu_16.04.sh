#!/usr/bin/env bash

set -ex

[ -n "$1" ] || ( echo "Install dir missing"; exit 1 )

sudo apt-get update

# Note libzstd-dev is not available on stock Ubuntu 14.04 or 15.04.
sudo apt-get install -y \
    autoconf \
    binutils-dev \
    bison \
    cmake \
    flex \
    g++ \
    gcc \
    git \
    libboost1.58-all-dev \
    libbz2-dev \
    libdouble-conversion-dev \
    libevent-dev \
    libgflags-dev \
    libgoogle-glog-dev \
    libjemalloc-dev \
    liblz4-dev \
    liblzma-dev \
    liblzma5 \
    libsnappy-dev \
    libsodium-dev \
    libssl-dev \
    libtool \
    libunwind8-dev \
    make \
    pkg-config \
    python-dev \
    ragel

cd "$(dirname "$0")" || ( echo "cd fail"; exit 1 )

./get_and_build_everything.sh ubuntu-16.04 "$@"
