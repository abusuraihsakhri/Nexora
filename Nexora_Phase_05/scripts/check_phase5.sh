#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")/.."
make clean
make check

echo
echo "Undefined freestanding references (expected only cross-object Nexora symbols):"
nm -u build/arch/*.o || true

echo
echo "Userspace ELF program headers:"
readelf -l build/user/demo.elf
