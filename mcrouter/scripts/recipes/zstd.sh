#!/usr/bin/env bash
# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

source common.sh

if [ ! -d "$PKG_DIR/zstd" ]; then
  git clone https://github.com/facebook/zstd
fi

cd "$PKG_DIR/zstd" || die "cd fail"

# Use a known compatible version
git checkout v1.4.9

cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" build/cmake/
make -j "$(nproc)" && make install
