#!/usr/bin/env bash

source common.sh

[ -d folly ] || git clone https://github.com/facebook/folly

if [ ! -d /usr/include/double-conversion ]; then
    if [ ! -d "$PKG_DIR/double-conversion" ]; then
        git clone https://github.com/google/double-conversion.git
    fi
    cd "$PKG_DIR/double-conversion" || die "cd fail"

    cmake . -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
    make $MAKE_ARGS && make install $MAKE_ARGS

    export LDFLAGS="-L$INSTALL_DIR/lib -ldl $LDFLAGS"
    export CPPFLAGS="-I$INSTALL_DIR/include $CPPFLAGS"
fi

cd "$PKG_DIR/folly/folly/" || die "cd fail"

autoreconf --install
LD_LIBRARY_PATH="$INSTALL_DIR/lib:$LD_LIBRARY_PATH" \
    LD_RUN_PATH="$INSTALL_DIR/lib:$LD_RUN_PATH" \
    ./configure --prefix="$INSTALL_DIR" && \
    make $MAKE_ARGS && make install $MAKE_ARGS
