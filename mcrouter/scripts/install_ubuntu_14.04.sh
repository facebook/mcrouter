#!/usr/bin/env bash

set -ex

[ -n "$1" ] || ( echo "Install dir missing"; exit 1 )

sudo apt-get update

sudo apt-get install -y gcc-4.8 g++-4.8 libboost1.54-dev libboost-thread1.54-dev \
    libboost-filesystem1.54-dev libboost-system1.54-dev libboost-regex1.54-dev \
    libboost-python1.54-dev libboost-context1.54-dev ragel autoconf unzip \
    libsasl2-dev git libtool python-dev cmake libssl-dev libcap-dev libevent-dev \
    libgtest-dev libsnappy-dev scons flex bison libkrb5-dev binutils-dev make \
    libnuma-dev wget

sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.8 50
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-4.8 50

cd $(dirname $0)

./get_and_build_everything.sh ubuntu-14.04 $1 $2
