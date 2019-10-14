#!/usr/bin/env bash
# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

source common.sh

if [[ ! -d "$PKG_DIR/rsocket-cpp" ]]; then
  git clone https://github.com/rsocket/rsocket-cpp
  cd "$PKG_DIR/rsocket-cpp" || die "cd fail"
  if [[ -f "$REPO_BASE_DIR/mcrouter/RSOCKET_COMMIT" ]]; then
    RSOCKET_COMMIT="$(head -n 1 "$REPO_BASE_DIR/mcrouter/RSOCKET_COMMIT")"
    echo "RSOCKET_COMMIT file found: using rsocket-cpp commit $RSOCKET_COMMIT"
    git checkout "$RSOCKET_COMMIT"
  else
    echo "No RSOCKET_COMMIT file, using rsocket-cpp HEAD=$(git rev-parse HEAD)"
  fi
fi

mkdir -p "$PKG_DIR/rsocket-cpp/build"
cd "$PKG_DIR/rsocket-cpp/build" || die "cd rsocket-cpp failed"

CXXFLAGS="$CXXFLAGS -fPIC" \
cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
  -DBUILD_BENCHMARKS=OFF \
  -DBUILD_EXAMPLES=OFF \
  -DBUILD_TESTS=OFF
make $MAKE_ARGS && make install $MAKE_ARGS

