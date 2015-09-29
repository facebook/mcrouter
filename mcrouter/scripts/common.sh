set -ex

function die { printf "%s: %s\n" "$0" "$@"; exit 1; }

[ -n "$1" ] || die "PKG_DIR missing"
[ -n "$2" ] || die "INSTALL_DIR missing"

PKG_DIR="$1"
INSTALL_DIR="$2"
shift 2
MAKE_ARGS="$@"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

mkdir -p "$PKG_DIR" "$INSTALL_DIR"

cd "$PKG_DIR" || die "cd fail"
