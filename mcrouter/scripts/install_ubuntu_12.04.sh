#!/usr/bin/env bash

set -ex

[ -n "$1" ] || ( echo "Install dir missing"; exit 1 )

echo "WARNING: mcrouter support for Ubuntu 12.04 will soon be phased out."
echo "We strongly encourage that you use mcrouter on Ubuntu 14.04 or newer."

sudo apt-get install -y python-software-properties
sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo add-apt-repository -y ppa:boost-latest/ppa
sudo add-apt-repository -y ppa:yjwong/gflags # gflags
sudo apt-get update

sudo apt-get install -y \
    autoconf \
    binutils-dev \
    cmake \
    flex \
    g++-4.8 \
    gcc-4.8 \
    git \
    libboost1.54-all-dev \
    libevent-dev \
    libgflags-dev \
    libjemalloc-dev \
    libssl-dev \
    libtool \
    make \
    pkg-config \
    python-dev \
    ragel

sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.8 50
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-4.8 50

cd "$(dirname "$0")" || ( echo "cd fail"; exit 1 )

./get_and_build_everything.sh ubuntu-12.04 "$@"
