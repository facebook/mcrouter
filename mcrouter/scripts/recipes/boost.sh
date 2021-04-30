#!/usr/bin/env bash
# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

source common.sh

if [ ! -d "$PKG_DIR/boost" ]; then
    wget https://boostorg.jfrog.io/artifactory/main/release/1.76.0/source/boost_1_76_0.tar.gz
    tar xzf boost_1_76_0.tar.gz
    mv boost_1_76_0 boost
    rm -f boost*.tar.gz
fi

cd "$PKG_DIR/boost" || die "cd fail"

./bootstrap.sh --prefix="$INSTALL_DIR"
./b2 -j "$(nproc)" --prefix="$INSTALL_DIR" install
