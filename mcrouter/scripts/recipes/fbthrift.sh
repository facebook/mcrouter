#!/usr/bin/env bash

source common.sh

if [[ ! -d "$PKG_DIR/fbthrift" ]]; then
  git clone https://github.com/facebook/fbthrift
  cd "$PKG_DIR/fbthrift" || die "cd fail"
  if [[ -f "$REPO_BASE_DIR/mcrouter/FBTHRIFT_COMMIT" ]]; then
    FBTHRIFT_COMMIT="$(head -n 1 "$REPO_BASE_DIR/mcrouter/FBTHRIFT_COMMIT")"
    echo "FBTHRIFT_COMMIT file found: using fbthrift commit $FBTHRIFT_COMMIT"
    git checkout "$FBTHRIFT_COMMIT"
  else
    echo "No FBTHRIFT_COMMIT file, using fbthrift HEAD=$(git rev-parse HEAD)"
  fi
fi

cd "$PKG_DIR/fbthrift/thrift" || die "cd fbthrift failed"

autoreconf --install
LD_LIBRARY_PATH="$INSTALL_DIR/lib:$LD_LIBRARY_PATH" \
    LD_RUN_PATH="$INSTALL_DIR/lib:$LD_RUN_PATH" \
    ./configure --prefix="$INSTALL_DIR" --bindir="$INSTALL_AUX_DIR/bin" \
                PY_PREFIX="$INSTALL_AUX_DIR" \
                PY_INSTALL_HOME="$INSTALL_AUX_DIR" \
                --without-cpp --with-folly="$INSTALL_DIR" && \
    make $MAKE_ARGS && make install $MAKE_ARGS
