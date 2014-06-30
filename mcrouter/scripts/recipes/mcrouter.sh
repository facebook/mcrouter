#!/usr/bin/env bash

source common.sh

cd "$SCRIPT_DIR/../.."
autoreconf --install
LD_LIBRARY_PATH="$INSTALL_DIR/lib:$LD_LIBRARY_PATH" \
    LD_RUN_PATH="$PKG_DIR/folly/folly/test/.libs:$INSTALL_DIR/lib" \
    LDFLAGS="-L$PKG_DIR/folly/folly/test/.libs -L$INSTALL_DIR/lib" \
    CPPFLAGS="-I$PKG_DIR/folly/folly/test/gtest-1.6.0/include -I$INSTALL_DIR/include -I$PKG_DIR/folly -I$PKG_DIR/fbthrift -I$PKG_DIR/double-conversion" \
    ./configure --prefix="$INSTALL_DIR"
# Need to find ragel
PATH="$INSTALL_DIR/bin:$PATH" make $MAKE_ARGS
make install $MAKE_ARGS
