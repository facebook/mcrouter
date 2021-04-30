#!/usr/bin/env bash
# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

set -ex

BASE_DIR="$1"
TARGET="${2:-all}"

[ -n "$BASE_DIR" ] || ( echo "Base dir missing"; exit 1 )

sudo yum install -y epel-release

sudo yum groupinstall -y "Development Tools"
sudo yum install -y \
    autoconf \
    binutils-devel \
    bison \
    bzip2-devel \
    cmake3 \
    double-conversion-devel \
    flex \
    gcc-c++\
    git \
    gtest-devel \
    jemalloc-devel \
    libevent-devel \
    libsodium-devel \
    libtool \
    libunwind-devel \
    lz4-devel \
    make \
    openssl-devel \
    python-devel \
    ragel \
    snappy-devel \
    xz-devel \
    zlib-devel

# The above dependencies provide the build time requirements
# for compiling "mcrouter" as well as a number of other dependencies.
#
# In order to package and deploy the resulting artifact, we need to ship
# mcrouter along with the compiled dynamic library dependencies.
# In addition, we need to install into the system the non development
# version of some libraries.  This comment is here to provide an example
# for what is necessary to provide a runtime environment.

#sudo yum install -y \
#  bzip2 \
#  double-conversion \
#  jemalloc \
#  libevent \
#  libsodium \
#  libunwind \
#  lz4 \
#  snappy \
#  openssl \
#  xz-libz \
#  zlib \


# Set CC and CXX to unambiguously choose compiler.
export CC=/usr/bin/gcc
export CXX=/usr/bin/c++

sudo ln -sf /usr/bin/cmake3 /usr/bin/cmake

# Automake available by default is 1.13 and unsupported for mcrouter.
# Install automake-1.15 from Fedora
yum info automake-1.15-4.fc23 || sudo yum install -y "http://archives.fedoraproject.org/pub/archive/fedora/linux/releases/23/Everything/x86_64/os/Packages/a/automake-1.15-4.fc23.noarch.rpm"

cd "$(dirname "$0")" || ( echo "cd fail"; exit 1 )

./get_and_build_by_make.sh "Makefile_amazon-linux-2" "$BASE_DIR" "$TARGET"
