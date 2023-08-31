#!/usr/bin/env bash


source common.sh

if [ ! -d "$PKG_DIR/mvfst" ]; then
    git clone https://github.com/facebook/mvfst.git "$PKG_DIR/mvfst"
    cd "$PKG_DIR/mvfst" || die "cd fail"

    cmake . \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DBUILD_TESTS=OFF
    make $MAKE_ARGS && make install $MAKE_ARGS
fi
