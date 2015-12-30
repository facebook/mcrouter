#!/usr/bin/env bash

source common.sh

pushd "$PKG_DIR"

# Required for fbthrift on Ubuntu 12.04
#
# Install all the automake packages:
#  - bison: The base system version is too old, does not make 'thrify.hh'
#  - automake: default 1.11.1 has bad bison support, does not make 'thrifty.hh'
#  - autoconf, libtool: newer so as to be compatible with the new automake
for url in \
    http://ftp.gnu.org/gnu/bison/bison-3.0.4.tar.gz \
    http://ftp.gnu.org/gnu/autoconf/autoconf-2.69.tar.gz \
    http://ftp.gnu.org/gnu/automake/automake-1.15.tar.gz \
    http://ftp.gnu.org/gnu/libtool/libtool-2.4.6.tar.gz \
    ; do
  pkg=$(basename "$url")
  wget "$url" -O "$pkg"
  tar xzf "$pkg"
  pushd "${pkg%.tar.gz}"
  ./configure
  make
  sudo make install
  popd
done

popd
