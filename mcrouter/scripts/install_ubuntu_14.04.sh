#!/usr/bin/env bash

set -ex

[ -n "$1" ] || ( echo "Install dir missing"; exit 1 )

sudo add-apt-repository ppa:ubuntu-toolchain-r/test

sudo apt-get update

sudo apt-get install -y \
    autoconf \
    binutils-dev \
    bison \
    cmake \
    flex \
    g++ \
    g++-4.9 \
    gcc \
    git \
    libboost1.54-all-dev \
    libdouble-conversion-dev \
    libevent-dev \
    libgflags-dev \
    libgoogle-glog-dev \
    libjemalloc-dev \
    libssl-dev \
    libtool \
    make \
    pkg-config \
    python-dev \
    ragel

cd "$(dirname "$0")" || ( echo "cd fail"; exit 1 )

export CC="gcc-4.9"
export CXX="g++-4.9"

./get_and_build_everything.sh ubuntu-14.04 "$@"
