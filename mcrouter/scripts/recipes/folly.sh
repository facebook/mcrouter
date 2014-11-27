#!/usr/bin/env bash

source common.sh

[ -d folly ] || git clone https://github.com/facebook/folly

git clone https://github.com/floitsch/double-conversion
cd "$PKG_DIR/double-conversion/"
scons prefix="$INSTALL_DIR" install

# Folly looks for double-conversion/double-conversion.h
ln -sf src double-conversion

cd "$PKG_DIR/folly/folly/test/"
grab http://googletest.googlecode.com/files/gtest-1.6.0.zip
unzip -o gtest-1.6.0.zip

cd "$PKG_DIR/folly/folly/"

autoreconf --install
LD_LIBRARY_PATH="$INSTALL_DIR/lib:$LD_LIBRARY_PATH" \
    LD_RUN_PATH="$INSTALL_DIR/lib" \
    LDFLAGS="-L$INSTALL_DIR/lib -L$PKG_DIR/double-conversion -ldl" \
    CPPFLAGS="-I$INSTALL_DIR/include -I$PKG_DIR/double-conversion" \
    ./configure --prefix="$INSTALL_DIR" && make $MAKE_ARGS && make install $MAKE_ARGS
