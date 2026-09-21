#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPORT="$ROOT/reports/CROSSCHECK_REPORT.txt"
BUILD="$ROOT/.crosscheck-build"
rm -rf "$BUILD"
mkdir -p "$BUILD" "$(dirname "$REPORT")"

exec > >(tee "$REPORT") 2>&1

echo "Nexora Phase 9 deep cross-check"
date -u '+UTC: %Y-%m-%dT%H:%M:%SZ'
echo

echo "[1/9] GCC host compile with warnings as errors"
gcc -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    -I"$ROOT/reference_tree/include" \
    "$ROOT/module/tests/phase9_host_test.c" \
    "$ROOT/module/src/ai/distributed.c" \
    -o "$BUILD/phase9_host_test"
echo "PASS"

echo
echo "[2/9] Host behavioral and adversarial tests"
"$BUILD/phase9_host_test"

echo
echo "[3/9] Clang portability compile/test when available"
if command -v clang >/dev/null 2>&1; then
    clang -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
        -I"$ROOT/reference_tree/include" \
        "$ROOT/module/tests/phase9_host_test.c" \
        "$ROOT/module/src/ai/distributed.c" \
        -o "$BUILD/phase9_host_test_clang"
    "$BUILD/phase9_host_test_clang"
    echo "PASS"
else
    echo "SKIP: clang not installed"
fi

echo
echo "[4/9] Address/undefined-behavior sanitizer run"
gcc -std=c11 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$ROOT/reference_tree/include" \
    "$ROOT/module/tests/phase9_host_test.c" \
    "$ROOT/module/src/ai/distributed.c" \
    -o "$BUILD/phase9_host_test_san"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$BUILD/phase9_host_test_san"
echo "PASS"

echo
echo "[5/9] Freestanding compile"
gcc -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
    -m64 -mno-red-zone -I"$ROOT/reference_tree/include" \
    -c "$ROOT/module/src/ai/distributed.c" \
    -o "$BUILD/distributed.o"
if command -v clang >/dev/null 2>&1; then
    clang -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
        -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
        -m64 -mno-red-zone -I"$ROOT/reference_tree/include" \
        -c "$ROOT/module/src/ai/distributed.c" \
        -o "$BUILD/distributed-clang.o"
fi
echo "PASS"

echo
echo "[6/9] Forbidden dependency and compiler-runtime scan"
if grep -nE '\b(malloc|calloc|realloc|free|socket|connect|send|recv|pthread_|printf|fprintf)\b' \
    "$ROOT/module/src/ai/distributed.c"; then
    echo "FAIL: forbidden kernel dependency found"
    exit 1
fi
if nm -u "$BUILD/distributed.o" | grep -E '(__udivti3|__divti3|malloc|calloc|realloc|free|socket|pthread_)'; then
    echo "FAIL: forbidden unresolved runtime symbol found"
    exit 1
fi
echo "PASS"

echo
echo "[7/9] Module/reference-tree mirror check"
cmp "$ROOT/module/include/ai/distributed.h" "$ROOT/reference_tree/include/ai/distributed.h"
cmp "$ROOT/module/src/ai/distributed.c" "$ROOT/reference_tree/src/ai/distributed.c"
echo "PASS"

echo
echo "[8/9] Build/link reference kernel ELF"
make -C "$ROOT/reference_tree" clean >/dev/null
make -C "$ROOT/reference_tree" build/kernel.elf
if nm -u "$ROOT/reference_tree/build/kernel.elf" | grep .; then
    echo "FAIL: linked kernel contains unresolved symbols"
    exit 1
fi
echo "PASS"

echo
echo "[9/9] Verify stable package SHA-256 manifest"
(
    cd "$ROOT"
    sha256sum -c reports/SHA256SUMS
)
echo "PASS"

echo
echo "RESULT: PASS — Phase 9 is internally consistent, adversarially checked, and baseline-ABI buildable."
