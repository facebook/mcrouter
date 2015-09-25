#!/usr/bin/env bash

source common.sh

grab https://google-glog.googlecode.com/files/glog-0.3.3.tar.gz
tar xzvf glog-0.3.3.tar.gz
cd "$PKG_DIR/glog-0.3.3" || die "cd fail"
LDFLAGS="-Wl,-rpath=$INSTALL_DIR/lib,--enable-new-dtags -L$INSTALL_DIR/lib" \
    CPPFLAGS="-I$INSTALL_DIR/include" \
    ./configure --prefix="$INSTALL_DIR" && \
    make $MAKE_ARGS && make install $MAKE_ARGS
