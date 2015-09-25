#!/usr/bin/env bash

source common.sh

[ -d folly ] || git clone https://github.com/facebook/folly

if [ ! -d /usr/include/double-conversion ]; then
    git clone https://github.com/floitsch/double-conversion
    cd "$PKG_DIR/double-conversion/" || die "cd fail"
    scons prefix="$INSTALL_DIR" install

    # Folly looks for double-conversion/double-conversion.h
    ln -sf src double-conversion

    export LDFLAGS="-L$INSTALL_DIR/lib -L$PKG_DIR/double-conversion -ldl"
    export CPPFLAGS="-I$INSTALL_DIR/include -I$PKG_DIR/double-conversion"
fi

cd "$PKG_DIR/folly/folly/" || die "cd fail"

autoreconf --install
LD_LIBRARY_PATH="$INSTALL_DIR/lib:$LD_LIBRARY_PATH" \
    LD_RUN_PATH="$INSTALL_DIR/lib" \
    ./configure --prefix="$INSTALL_DIR" && \
    make $MAKE_ARGS && make install $MAKE_ARGS
