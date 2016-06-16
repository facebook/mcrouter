#!/usr/bin/env bash

set -ex

[ -n "$1" ] || ( echo "Install dir missing"; exit 1 )

sudo yum install -y epel-release

sudo yum install -y \
    autoconf \
    binutils-devel \
    bison \
    boost-devel \
    double-conversion-devel \
    flex \
    gcc-c++ \
    gcc \
    git \
    gflags-devel \
    glog-devel \
    jemalloc-devel \
    openssl-devel \
    libtool \
    libevent-devel \
    make \
    python-devel 

cd "$(dirname "$0")" || ( echo "cd fail"; exit 1 )

./get_and_build_everything.sh centos-7.2 "$@"
