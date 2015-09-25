#!/usr/bin/env bash

source common.sh

grab https://github.com/schuhschuh/gflags/archive/v2.1.1.tar.gz gflags-2.1.1.tar.gz
tar xzvf gflags-2.1.1.tar.gz
mkdir -p gflags-2.1.1/build/
cd gflags-2.1.1/build/ || die "cd fail"

if which cmake28; then
  CMAKE=cmake28
else
  # We need a brand new cmake, so build one
  grab http://www.cmake.org/files/v2.8/cmake-2.8.12.1.tar.gz
  tar xzvf cmake-2.8.12.1.tar.gz
  (
    cd cmake-2.8.12.1
    cmake . && make
    cd ..
  )
  CMAKE=./cmake-2.8.12.1/bin/cmake
fi
$CMAKE .. -DBUILD_SHARED_LIBS:BOOL=ON \
  -DCMAKE_INSTALL_PREFIX:PATH="$INSTALL_DIR" \
  -DGFLAGS_NAMESPACE:STRING=google && \
  make $MAKE_ARGS && make install $MAKE_ARGS
