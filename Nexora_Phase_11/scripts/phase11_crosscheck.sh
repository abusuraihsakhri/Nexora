#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

printf '%s\n' '[1/9] deterministic + property host tests'
make phase11-test

printf '%s\n' '[2/9] ASan + UBSan host tests'
make phase11-sanitize

printf '%s\n' '[3/9] GCC static analyzer'
if gcc -std=c11 -O0 -Wall -Wextra -Werror -fanalyzer -Iinclude \
       -c src/ai/policy.c -o build/policy_analyzer.o; then
    echo 'gcc -fanalyzer: PASS'
else
    echo 'gcc -fanalyzer: FAIL'
    exit 1
fi

printf '%s\n' '[4/9] freestanding policy compile'
mkdir -p build/crosscheck
gcc -std=c11 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
    -m64 -mno-red-zone -Iinclude \
    -c src/ai/policy.c -o build/crosscheck/policy.o

printf '%s\n' '[5/9] clean full kernel ELF link'
make clean >/dev/null
make build/kernel.elf

printf '%s\n' '[6/9] dependency check'
if nm -u build/kernel.elf | grep -E '(^| )((malloc|calloc|realloc|free|printf|puts|memcpy|memset|strcmp|strlen))$' >/dev/null; then
    echo 'unexpected libc dependency in kernel'
    exit 1
fi

printf '%s\n' '[7/9] Phase 11.1 regression-source checks'
grep -q 'AI_POLICY_INVARIANT_BAD_EFFECT' include/ai/policy.h
grep -q 'ai_policy_reclaim_inactive' include/ai/policy.h
grep -q 'request_resource_valid' src/ai/policy.c
grep -q 'Management-operation trust contract' include/ai/policy.h
grep -q 'Synchronization contract' include/ai/policy.h
grep -q 'Per-request byte ceiling' include/ai/policy.h
grep -q 'AI_POLICY_ANY_ID is a policy wildcard sentinel' src/ai/policy.c

printf '%s\n' '[8/9] packaged source checksum validation'
if [[ -f SHA256SUMS ]]; then
    sha256sum -c SHA256SUMS
else
    echo 'SHA256SUMS unavailable: SKIP while developing; required in release package'
fi

printf '%s\n' '[9/9] optional Multiboot2/QEMU tooling status'
if command -v grub-file >/dev/null 2>&1; then
    grub-file --is-x86-multiboot2 build/kernel.elf
    echo 'multiboot2 header: PASS'
else
    echo 'grub-file unavailable: SKIP (kernel ELF link already passed)'
fi
if command -v qemu-system-x86_64 >/dev/null 2>&1 && command -v grub-mkrescue >/dev/null 2>&1; then
    echo 'QEMU/GRUB tooling detected; interactive boot remains available via make run.'
else
    echo 'QEMU/GRUB ISO tooling incomplete: boot execution SKIP.'
fi

echo 'Phase 11.1 cross-check: PASS'
