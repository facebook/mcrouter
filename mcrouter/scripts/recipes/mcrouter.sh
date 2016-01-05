#!/usr/bin/env bash

source common.sh

# clone the latest gtest
[ -d googletest ] || git clone https://github.com/google/googletest.git

cd "$SCRIPT_DIR/../.." || die "cd fail"

# copy gtest source into lib/gtest folder
mkdir -p ./lib/gtest
cp -r -f -t ./lib/gtest "$PKG_DIR/googletest/googletest"/*

THRIFT2_COMPILER=$(readlink -f \
  "$(find "$INSTALL_AUX_DIR" | grep -m 1 'thrift_compiler/main\.py$')")

test "x$THRIFT2_COMPILER" != "x" || die "Couldn't find fbthrift cpp2 compiler"

THRIFT2_COMP_DIR="$(dirname "$THRIFT2_COMPILER")"

autoreconf --install
LD_LIBRARY_PATH="$INSTALL_DIR/lib:$LD_LIBRARY_PATH" \
    LD_RUN_PATH="$INSTALL_DIR/lib:$LD_RUN_PATH" \
    LDFLAGS="-L$INSTALL_DIR/lib $LDFLAGS" \
    CPPFLAGS="-I$INSTALL_DIR/include $CPPFLAGS" \
    ./configure --prefix="$INSTALL_DIR" THRIFT2_COMP_DIR="$THRIFT2_COMP_DIR"
make $MAKE_ARGS && make install $MAKE_ARGS
