#!/usr/bin/env bash

set -ex

ORDER="$1"
PKG_DIR="${2%/}"/pkgs
INSTALL_DIR="${2%/}"/install
shift 2

mkdir -p "$PKG_DIR" "$INSTALL_DIR"

cd "$(dirname "$0")" || ( echo "cd fail"; exit 1 )

for script in $(ls "order_$ORDER/" | egrep '^[0-9]+_.*[^~]$' | sort -n); do
  "./order_$ORDER/$script" "$PKG_DIR" "$INSTALL_DIR" "$@"
done

printf "%s\n" "Mcrouter installed in $INSTALL_DIR/bin/mcrouter"
