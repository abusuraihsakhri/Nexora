#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

printf '%s\n' '== Phase 10 cross-check =='
sha256sum -c SHA256SUMS
make clean
make crosscheck

rm -rf build-crosscheck
cmake -S . -B build-crosscheck -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build build-crosscheck -j2 >/dev/null
ctest --test-dir build-crosscheck --output-on-failure

printf '%s\n' 'Phase 10 independent cross-check: PASS'
