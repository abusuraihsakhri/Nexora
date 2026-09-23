#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo "[1/8] strict compile + required symbols"
make phase7-check

echo "[2/8] checking required files"
for f in \
  include/arch/x86_64/io.h \
  include/kernel/pci.h \
  include/kernel/dma.h \
  include/ai/accelerator.h \
  include/drivers/nex_accel_sim.h \
  include/drivers/pci_accel.h \
  src/drivers/pci.c \
  src/drivers/nex_accel_sim.c \
  src/drivers/pci_accel.c \
  src/mm/dma.c \
  src/ai/accelerator.c \
  docs/PHASE7_REAL_DEVICE_PATH.md \
  docs/PHASE7_VALIDATION.md \
  docs/PHASE7_INTEGRATION_NOTES.md; do
  test -s "$f"
done

echo "[3/8] checking simulator does not claim real AI execution"
grep -q 'AI_ACCEL_STATUS_SIMULATED' src/drivers/nex_accel_sim.c
grep -q 'not real numerical execution' src/drivers/nex_accel_sim.c

echo "[4/8] checking physical PCI candidates stay offline"
grep -q 'device->online = false' src/drivers/pci_accel.c

echo "[5/8] checking no vendor GPU API dependency was introduced"
if grep -RniE '#include[[:space:]]*<.*(cuda|hip|opencl|vulkan)|cuInit\(|hipInit\(|clGetPlatformIDs\(' include src; then
  echo "Unexpected vendor/runtime API dependency detected" >&2
  exit 1
fi

echo "[6/8] checking ELF architecture and unresolved symbols"
readelf -h build/kernel.elf | grep -q 'Machine:.*Advanced Micro Devices X86-64'
if [[ -n "$(nm -u build/kernel.elf)" ]]; then
  echo "Unresolved kernel symbols detected" >&2
  nm -u build/kernel.elf >&2
  exit 1
fi

echo "[7/8] independently verifying Multiboot2 header"
python3 scripts/verify_multiboot2.py build/kernel.elf

echo "[8/8] checking linked addresses remain inside initial identity map"
entry_hex="$(readelf -h build/kernel.elf | awk '/Entry point address:/ {print $4}')"
entry=$((entry_hex))
if (( entry < 0x100000 || entry >= 0x40000000 )); then
  echo "Kernel entry point lies outside expected 1 MiB..1 GiB identity-mapped range: $entry_hex" >&2
  exit 1
fi

echo "Phase 7 cross-check: PASS"
