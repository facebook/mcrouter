#!/usr/bin/env bash

source common.sh

if [[ ! -d folly ]]; then
  git clone https://github.com/facebook/folly
  cd "$PKG_DIR/folly" || die "cd fail"
  if [[ -f "$REPO_BASE_DIR/mcrouter/FOLLY_COMMIT" ]]; then
    FOLLY_COMMIT="$(head -n 1 "$REPO_BASE_DIR/mcrouter/FOLLY_COMMIT")"
    echo "FOLLY_COMMIT file found: using folly commit $FOLLY_COMMIT"
    git checkout "$FOLLY_COMMIT"
  else
    echo "No FOLLY_COMMIT file, using folly HEAD=$(git rev-parse HEAD)"
  fi
fi

if [ ! -d /usr/include/double-conversion ]; then
  if [ ! -d "$PKG_DIR/double-conversion" ]; then
    cd "$PKG_DIR" || die "cd fail"
    git clone https://github.com/google/double-conversion.git
  fi
  cd "$PKG_DIR/double-conversion" || die "cd fail"

  # Workaround double-conversion CMakeLists.txt changes that
  # are incompatible with cmake-2.8
  git checkout ea970f69edacf66bd3cba2892be284b76e9599b0
  cmake . -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"
  make $MAKE_ARGS && make install $MAKE_ARGS

  export LDFLAGS="-L$INSTALL_DIR/lib -ldl $LDFLAGS"
  export CPPFLAGS="-I$INSTALL_DIR/include $CPPFLAGS"
fi

if [ ! -d "$PKG_DIR/zstd" ]; then
  cd "$PKG_DIR" || die "cd fail"
  git clone https://github.com/facebook/zstd

  cd "$PKG_DIR/zstd" || die "cd fail"

  # Checkout zstd-1.3.7 release
  git checkout 21cd8a9d95a321f1fe256dc837e388bbc168fdbf
  cmake -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" build/cmake/
  make $MAKE_ARGS && make install $MAKE_ARGS
fi

cd "$PKG_DIR/folly/folly/" || die "cd fail"

CXXFLAGS="$CXXFLAGS -fPIC" \
    LD_LIBRARY_PATH="$INSTALL_DIR/lib:$LD_LIBRARY_PATH" \
    LD_RUN_PATH="$INSTALL_DIR/lib:$LD_RUN_PATH" \
    cmake .. \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
    -DCMAKE_INCLUDE_PATH="$INSTALL_DIR/lib" \
    -DCMAKE_LIBRARY_PATH="$INSTALL_DIR/lib"
make $MAKE_ARGS && make install $MAKE_ARGS
