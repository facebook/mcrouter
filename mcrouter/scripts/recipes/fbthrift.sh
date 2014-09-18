#!/usr/bin/env bash

source common.sh

[ -d fbthrift ] || git clone https://github.com/facebook/fbthrift
cd fbthrift/thrift
# Fix build
sed 's/PKG_CHECK_MODULES.*$/true/g' -i configure.ac
ln -sf thrifty.h "$PKG_DIR/fbthrift/thrift/compiler/thrifty.hh"
autoreconf --install
# LD_LIBRARY_PATH is needed since configure builds small programs with -lgflags, and doesn't use
# libtool to encode full library path, so running those will not find libgflags otherwise
# We need --enable-boostthreads, since otherwise thrift will not link against
# -lboost_thread, while still using functions from it (need to fix thrift directly)
LD_LIBRARY_PATH="$INSTALL_DIR/lib:$LD_LIBRARY_PATH" \
    LD_RUN_PATH="$INSTALL_DIR/lib" \
    LDFLAGS="-L$INSTALL_DIR/lib" \
    CPPFLAGS="-I$INSTALL_DIR/include -I$INSTALL_DIR/include/python2.7 -I$PKG_DIR/folly -I$PKG_DIR/double-conversion" \
    ./configure --prefix=$INSTALL_DIR --enable-boostthreads
cd "$PKG_DIR/fbthrift/thrift/" && make clean
cd "$PKG_DIR/fbthrift/thrift/compiler" && make $MAKE_ARGS
cd "$PKG_DIR/fbthrift/thrift/lib/thrift" && make $MAKE_ARGS
cd "$PKG_DIR/fbthrift/thrift/lib/cpp2" && make gen-cpp2/Sasl_types.h $MAKE_ARGS
cd "$PKG_DIR/fbthrift/thrift/lib/cpp2/test" && make gen-cpp2/Service_constants.cpp $MAKE_ARGS
cd "$PKG_DIR/fbthrift/thrift" && make $MAKE_ARGS && sudo make install $MAKE_ARGS
