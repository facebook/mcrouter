#!/usr/bin/env bash

set -ex

sudo apt-get install -y libdouble-conversion1 libgflags2 \
    libboost-program-options1.54.0 libboost-filesystem1.54.0 \
    libboost-system1.54.0 libboost-regex1.54.0 libboost-thread1.54.0 \
    libboost-context1.54.0 libgoogle-glog0 libevent-2.0-5 libsnappy1

sudo apt-get purge -y gcc-4.8 g++-4.8 libboost1.54-dev libboost-thread1.54-dev \
    libboost-program-options1.54-dev libboost-filesystem1.54-dev \
    libboost-system1.54-dev libboost-regex1.54-dev \
    libboost-python1.54-dev libboost-context1.54-dev ragel autoconf unzip \
    git libtool python-dev cmake libssl-dev libcap-dev libevent-dev \
    libgtest-dev libsnappy-dev scons binutils-dev make \
    wget libdouble-conversion-dev libgflags-dev libgoogle-glog-dev

sudo apt-get purge -y 'libboost.*-dev'
sudo apt-get autoremove --purge -y
sudo apt-get autoclean -y
sudo apt-get clean -y

if [[ "x$1" != "x" ]]; then
    PKG_DIR=$1/pkgs
    INSTALL_DIR=$1/install
    strip "$INSTALL_DIR"/bin/mcrouter
    rm -rf "$PKG_DIR"
    rm -rf "$INSTALL_DIR"/lib/*.a
    rm -rf "$INSTALL_DIR"/include
fi
