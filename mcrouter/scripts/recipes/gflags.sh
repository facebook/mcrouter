#!/usr/bin/env bash
# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

source common.sh

if [ ! -d "$PKG_DIR/gflags" ]; then
    git clone https://github.com/gflags/gflags.git
fi

cd "$PKG_DIR/gflags" || die "cd fail"

# Use a known compatible version
# There hasn't been a release in years, this is just the (currently) most
# recent commit.
git checkout 827c769e5fc98e0f2a34c47cef953cc6328abced

LDFLAGS="-Wl,-rpath=$INSTALL_DIR/lib,--enable-new-dtags -L$INSTALL_DIR/lib $LDFLAGS" \
    CPPFLAGS="-I$INSTALL_DIR/include -DGOOGLE_GLOG_DLL_DECL='' $CPPFLAGS" \
    cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_SHARED_LIBS=YES -S . -B build -G "Unix Makefiles"

cmake --build build --target install
