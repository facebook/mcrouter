#!/usr/bin/env bash
# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

source common.sh

if [ ! -d "$PKG_DIR/fizz" ]; then
  git clone https://github.com/facebookincubator/fizz
fi

cd "$PKG_DIR/fizz" || die "cd fail"

# Use a known compatible version
git checkout v2021.04.26.00

cd "$PKG_DIR/fizz/fizz/" || die "cd fail"

cmake . -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_TESTS=OFF
make -j "$(nproc)" && make install
