#!/usr/bin/env bash
# Copyright (c) Facebook, Inc. and its affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

set -ex

MAKE_FILE="$1"
TARGET="$3"
PKG_DIR="${2%/}"/pkgs
INSTALL_DIR="${2%/}"/install
INSTALL_AUX_DIR="${2%/}"/install/aux

[ -n "$MAKE_FILE" ] || ( echo "Make file missing"; exit 1 )
[ -n "$TARGET" ] || ( echo "Target missing"; exit 1 )

mkdir -p "$PKG_DIR" "$INSTALL_DIR" "$INSTALL_AUX_DIR"
mkdir -p "$INSTALL_DIR/lib"

# The recipes/mcrouter.sh build script fails at convincing the compiler and linker
# to look at $INSTALL_DIR/lib64
# As a workaround, we just upfront link lib64 -> lib so all dependency artifacts
# end up in $INSTALL_DIR/lib which that build script *can* find.
if [ ! -e "$INSTALL_DIR/lib64" ]; then
    ln -sf "$INSTALL_DIR/lib" "$INSTALL_DIR/lib64"
fi

cd "$(dirname "$0")" || ( echo "cd fail"; exit 1 )

REPO_BASE_DIR="$(cd ../../ && pwd)" || die "Couldn't determine repo top dir"
export REPO_BASE_DIR

export LDFLAGS="-ljemalloc $LDFLAGS"

make "$TARGET" -j "$(nproc)" -f "$MAKE_FILE" PKG_DIR="$PKG_DIR" INSTALL_DIR="$INSTALL_DIR" INSTALL_AUX_DIR="$INSTALL_AUX_DIR"

printf "%s\n" "make $TARGET for $MAKE_FILE done"
