#!/usr/bin/env bash

source common.sh

if [ ! -d "$PKG_DIR/glog" ]; then
    git clone https://github.com/google/glog.git
    cd "$PKG_DIR/glog" || die "cd fail"

    # dirty hack until the issue is fixed upstream
    sed -i.bak "s/namespace gflags/namespace GFLAGS_NAMESPACE/g" \
      src/signalhandler_unittest.cc

    autoreconf --install
    LDFLAGS="-Wl,-rpath=$INSTALL_DIR/lib,--enable-new-dtags \
             -L$INSTALL_DIR/lib $LDFLAGS" \
        CPPFLAGS="-I$INSTALL_DIR/include -DGOOGLE_GLOG_DLL_DECL='' \
                  -DGFLAGS_NAMESPACE=google \
                  $CPPFLAGS" \
        ./configure --prefix="$INSTALL_DIR" && \
        make $MAKE_ARGS && make install $MAKE_ARGS
fi
