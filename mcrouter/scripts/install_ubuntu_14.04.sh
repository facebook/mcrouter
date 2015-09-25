#!/usr/bin/env bash

set -ex

[ -n "$1" ] || ( echo "Install dir missing"; exit 1 )

sudo apt-get update

sudo apt-get install -y gcc-4.8 g++-4.8 libboost1.54-dev libboost-thread1.54-dev \
    libboost-program-options1.54-dev libboost-filesystem1.54-dev \
    libboost-system1.54-dev libboost-regex1.54-dev \
    libboost-python1.54-dev libboost-context1.54-dev ragel autoconf unzip \
    git libtool python-dev cmake libssl-dev libcap-dev libevent-dev \
    libsnappy-dev scons binutils-dev make \
    wget libdouble-conversion-dev libgflags-dev libgoogle-glog-dev

sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.8 50
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-4.8 50

cd "$(dirname "$0")" || ( echo "cd fail"; exit 1 )

./get_and_build_everything.sh ubuntu-14.04 "$@"
