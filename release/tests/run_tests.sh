#!/usr/bin/env sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD="$ROOT/build"
mkdir -p "$BUILD"
CC=${CC:-cc}
CFLAGS=${CFLAGS:--std=c11 -O2 -Wall -Wextra -Werror -pedantic}
$CC $CFLAGS -I"$ROOT/include" \
  "$ROOT/src/phase17/health.c" \
  "$ROOT/src/phase17/trace.c" \
  "$ROOT/src/phase17/watchdog.c" \
  "$ROOT/src/phase17/build_info.c" \
  "$ROOT/tests/test_phase17.c" \
  -o "$BUILD/test_phase17"
"$BUILD/test_phase17"
