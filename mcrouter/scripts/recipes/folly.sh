#!/usr/bin/env bash
# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

source common.sh

if [ ! -d "$PKG_DIR/folly" ]; then
  git clone https://github.com/facebook/folly
fi

cd "$PKG_DIR/folly" || die "cd fail"

# Use a known compatible version
git checkout v2021.04.26.00

# There is an issue when compiling folly on aarch64 when libunwind is available.
# The build configuration does expose a direct way to avoid using libunwind, and we
# need to have it installed for other dependencies, so we just edit the cmake config
# file to force this variable to OFF.
sed -i 's/set(FOLLY_HAVE_LIBUNWIND ON)/set(FOLLY_HAVE_LIBUNWIND OFF)/' CMake/folly-deps.cmake

cd "$PKG_DIR/folly/folly/" || die "cd fail"

CXXFLAGS="$CXXFLAGS -fPIC" \
    LD_LIBRARY_PATH="$INSTALL_DIR/lib:$LD_LIBRARY_PATH" \
    LD_RUN_PATH="$INSTALL_DIR/lib:$LD_RUN_PATH" \
    cmake .. \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_INCLUDE_PATH="$INSTALL_DIR/lib" \
    -DCMAKE_LIBRARY_PATH="$INSTALL_DIR/lib"
make -j "$(nproc)" && make install
