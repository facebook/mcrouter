#!/usr/bin/env bash

set -ex

[ -n "$1" ] || ( echo "Install dir missing"; exit 1 )

sudo apt-get install python-software-properties
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo add-apt-repository ppa:boost-latest/ppa
sudo apt-get update

sudo apt-get install gcc-4.8 g++-4.8 libboost1.54-dev libboost-thread1.54-dev \
    ragel autoconf unzip libsasl2-dev git libtool python-dev cmake libssl-dev \
    libcap-dev libevent-dev libgtest-dev libsnappy-dev scons flex bison \
    libkrb5-dev binutils-dev

sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.8 50
sudo update-alternatives --install /usr/bin/g++ gcc /usr/bin/g++-4.8 50

./get_and_build_everything.sh ubuntu-12.04 $1 $2
