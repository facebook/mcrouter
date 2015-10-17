#!/usr/bin/env bash

set -ex

[ -n "$1" ] || ( echo "Install dir missing"; exit 1 )

sudo apt-get update

sudo apt-get install -y \
    autoconf \
    binutils-dev \
    g++ \
    gcc \
    git \
    libboost-context1.54-dev \
    libboost-filesystem1.54-dev \
    libboost-program-options1.54-dev \
    libboost-regex1.54-dev \
    libboost-system1.54-dev \
    libboost-thread1.54-dev \
    libboost1.54-dev \
    libdouble-conversion-dev \
    libevent-dev \
    libgflags-dev \
    libgoogle-glog-dev \
    libjemalloc-dev \
    libssl-dev \
    libtool \
    make \
    python-dev \
    ragel

cd "$(dirname "$0")" || ( echo "cd fail"; exit 1 )

./get_and_build_everything.sh ubuntu-14.04 "$@"
