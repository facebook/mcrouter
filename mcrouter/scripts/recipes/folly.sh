#!/usr/bin/env bash

source common.sh

[ -d folly ] || git clone https://github.com/facebook/folly

mkdir -p double-conversion && cd double-conversion
grab https://double-conversion.googlecode.com/files/double-conversion-2.0.1.tar.gz

# Careful - tar has no containing dir, files flying everywhere
tar xzvf double-conversion-2.0.1.tar.gz
cp "$PKG_DIR/folly/folly/SConstruct.double-conversion" .
scons -f SConstruct.double-conversion
# Folly looks for double-conversion/double-conversion.h
ln -sf src double-conversion
# Folly looks for -ldouble-conversion (dash, not underscore)
# Must be PIC, since folly also builds a shared library
ln -sf libdouble_conversion_pic.a libdouble-conversion.a

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
