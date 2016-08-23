#!/usr/bin/env bash

source common.sh

pushd "$PKG_DIR"

# Required for Centos 7 build
#
# - base repo has cmake-2.8.11, cmake-2.8.12 is needed for wangle build
# - ragel centos7 rpm not found, need to build from source

sudo rpm -Uvh "https://kojipkgs.fedoraproject.org/packages/automake/1.15/1.fc22/noarch/automake-1.15-1.fc22.noarch.rpm"

for url in \
    https://cmake.org/files/v2.8/cmake-2.8.12.2.tar.gz \
    http://www.colm.net/files/ragel/ragel-6.8.tar.gz \
    ; do
  pkg=$(basename "$url")
  wget "$url" -O "$pkg"
  tar xzf "$pkg"
  pushd "${pkg%.tar.gz}"
  if [ -f ./bootstrap ]; then
    ./bootstrap
  elif [ -f ./configure ]; then
    ./configure
  fi;
  make
  sudo make install
  popd
done
popd

