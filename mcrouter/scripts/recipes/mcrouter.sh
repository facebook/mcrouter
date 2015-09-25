#!/usr/bin/env bash

source common.sh

# download and unzip latest gtest
if [ ! -d "$PKG_DIR/gtest" ]; then
    mkdir -p "$PKG_DIR/gtest"
    grab https://github.com/google/googletest/archive/release-1.7.0.zip \
      "$PKG_DIR/gtest/gtest-1.7.0.zip"
    unzip -o "$PKG_DIR/gtest/gtest-1.7.0" -d "$PKG_DIR/gtest"
fi

cd "$SCRIPT_DIR/../.." || die "cd fail"

# copy gtest source into lib/gtest folder
mkdir -p ./lib/gtest
cp -r -t ./lib/gtest "$PKG_DIR/gtest/googletest-release-1.7.0"/*

autoreconf --install
LD_LIBRARY_PATH="$INSTALL_DIR/lib:$LD_LIBRARY_PATH" \
    LD_RUN_PATH="$PKG_DIR/folly/folly/test/.libs:$INSTALL_DIR/lib" \
    LDFLAGS="-L$PKG_DIR/folly/folly/test/.libs -L$INSTALL_DIR/lib" \
    CPPFLAGS="-I$INSTALL_DIR/include -I$PKG_DIR/double-conversion" \
    ./configure --prefix="$INSTALL_DIR"
make $MAKE_ARGS && make install $MAKE_ARGS
