#!/bin/bash
# Spanfall build (WSL). Usage:
#   ./build.sh          release build  -> build/spanfall.vpk
#   ./build.sh pretend  updater test   -> build-pretend/spanfall.vpk  (the updater believes it is
#                       0.0.0, so "Check for updates" offers the real published release).
#                       NEVER release this one.
set -euo pipefail

export VITASDK="$HOME/vitasdk"
export PATH="$VITASDK/bin:$HOME/tools/cmake-3.30.5-linux-x86_64/bin:$PATH"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODE="${1:-release}"

PRETEND=""
case "$MODE" in
  release) BUILD_DIR="$ROOT/build" ;;
  pretend) BUILD_DIR="$ROOT/build-pretend"; PRETEND="0.0.0" ;;
  *) echo "usage: $0 [pretend]" >&2; exit 2 ;;
esac

cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
      -DSF_UPDATER_PRETEND_VERSION="$PRETEND"
cmake --build "$BUILD_DIR" -j"$(nproc)"

VPK="$BUILD_DIR/spanfall.vpk"
if [ ! -f "$VPK" ]; then
  echo "ERROR: $VPK was not produced" >&2
  exit 1
fi

echo "VPK:  $VPK"
echo "size: $(stat -c %s "$VPK") bytes"
echo "md5:  $(md5sum "$VPK" | cut -d' ' -f1)"
