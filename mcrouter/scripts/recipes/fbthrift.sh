#!/usr/bin/env bash

source common.sh

[ -d fbthrift ] || git clone https://github.com/facebook/fbthrift
cd fbthrift/thrift
# Fix build
sed 's/PKG_CHECK_MODULES.*$/true/g' -i configure.ac
autoreconf --install
# LD_LIBRARY_PATH is needed since configure builds small programs with -lgflags, and doesn't use
# libtool to encode full library path, so running those will not find libgflags otherwise
LD_LIBRARY_PATH="$INSTALL_DIR/lib:$LD_LIBRARY_PATH" \
    LD_RUN_PATH="$INSTALL_DIR/lib" \
    LDFLAGS="-L$INSTALL_DIR/lib" \
    CPPFLAGS="-I$INSTALL_DIR/include -I$INSTALL_DIR/include/python2.7 -I$PKG_DIR/folly -I$PKG_DIR/double-conversion" \
    ./configure --prefix=$INSTALL_DIR

# Circular and missing dependencies.  Awesome.
cd compiler && make libparse.la libthriftcompilerbase.la $MAKE_ARGS
cd py && make libfrontend.la $MAKE_ARGS
cd "$PKG_DIR/fbthrift/thrift/compiler" && make $MAKE_ARGS
cd "$PKG_DIR/fbthrift/thrift/lib/thrift" && make $MAKE_ARGS
cd "$PKG_DIR/fbthrift/thrift/lib/cpp2" && make gen-cpp2/Sasl_types.h $MAKE_ARGS
cd "$PKG_DIR/fbthrift/thrift/lib/cpp2/test" && make gen-cpp2/Service_constants.cpp $MAKE_ARGS
cd "$PKG_DIR/fbthrift/thrift" && make $MAKE_ARGS && make install $MAKE_ARGS
