#!/usr/bin/env bash
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

set -ex

[ -n "$1" ] || ( echo "Install dir missing"; exit 1 )

sudo apt-get update

sudo apt-get --no-install-recommends install -y \
    autoconf \
    binutils-dev \
    bison \
    cmake \
    flex \
    g++ \
    gcc \
    git \
    libboost1.55-all-dev \
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
    make \
    pkg-config \
    python-dev \
    ragel

cd "$(dirname "$0")" || ( echo "cd fail"; exit 1 )

./get_and_build_everything.sh ubuntu-15.04 "$@"

# Some Cleanup Up On the go
sudo apt-get clean

sudo apt-get -y --purge autoremove
