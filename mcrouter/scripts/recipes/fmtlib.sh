#!/usr/bin/env bash

source common.sh

if [[ ! -d "$PKG_DIR/fmt" ]]; then
  git clone https://github.com/fmtlib/fmt
  cd "$PKG_DIR/fmt" || die "cd failed"
  mkdir "$PKG_DIR/fmt/build"
fi

cd "$PKG_DIR/fmt/build" || die "cd fmt failed"

CXXFLAGS="$CXXFLAGS -fPIC" \
  cmake .. -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
make $MAKE_ARGS && make install $MAKE_ARGS

