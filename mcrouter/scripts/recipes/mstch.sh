#!/usr/bin/env bash

source common.sh

[ -d mstch ] || git clone https://github.com/no1msd/mstch

cd "mstch/" || "cd fail"

cmake -DCMAKE_INSTALL_PREFIX:PATH="$INSTALL_DIR" .
make $MAKE_ARGS && make install $MAKE_ARGS
