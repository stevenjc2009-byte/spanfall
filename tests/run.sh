#!/bin/bash
# Host tests for the game rules (sand, pieces, scoring). Usage: tests/run.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/tests/build"
mkdir -p "$OUT"
"${CC:-gcc}" -std=gnu11 -O2 -Wall -Wextra -Werror -o "$OUT/run_tests" \
    "$ROOT/src/sand.c" "$ROOT/src/piece.c" "$ROOT/src/game.c" "$ROOT/src/save.c" "$ROOT"/tests/*.c
cd "$ROOT" && "$OUT/run_tests"
