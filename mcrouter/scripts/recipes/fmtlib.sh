#!/usr/bin/env bash
# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

source common.sh

if [ ! -d "$PKG_DIR/fmt" ]; then
  git clone https://github.com/fmtlib/fmt
fi

cd "$PKG_DIR/fmt" || die "cd failed"

# Use a known compatible version
git checkout 7.1.3

mkdir "$PKG_DIR/fmt/build"
cd "$PKG_DIR/fmt/build" || die "cd fmt failed"

CXXFLAGS="$CXXFLAGS -fPIC" \
  cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
make -j "$(nproc)" && make install

