#!/usr/bin/env bash
# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

source common.sh

if [ ! -d wangle ]; then
  git clone https://github.com/facebook/wangle
fi

cd "$PKG_DIR/wangle" || die "cd fail"

# Use a known compatible version
git reset --hard v2021.04.26.00

cd "$PKG_DIR/wangle/wangle/" || die "cd fail"

cmake . -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_TESTS=OFF
make -j "$(nproc)" && make install
