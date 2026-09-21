#!/usr/bin/env python3
"""Minimal independent Multiboot2-header verifier for the Nexora kernel ELF."""
from __future__ import annotations

import struct
import sys
from pathlib import Path

MAGIC = 0xE85250D6
SEARCH_LIMIT = 32768


def verify(path: Path) -> None:
    data = path.read_bytes()[:SEARCH_LIMIT]
    for offset in range(0, max(0, len(data) - 16) + 1, 8):
        magic, arch, length, checksum = struct.unpack_from("<IIII", data, offset)
        if magic != MAGIC:
            continue
        if arch != 0:
            raise SystemExit(f"Multiboot2 header at {offset:#x} has unsupported arch {arch}")
        if length < 24 or offset + length > SEARCH_LIMIT or offset + length > len(data):
            raise SystemExit(f"Multiboot2 header at {offset:#x} has invalid length {length}")
        if (magic + arch + length + checksum) & 0xFFFFFFFF:
            raise SystemExit("Multiboot2 checksum mismatch")
        end_type, end_flags, end_size = struct.unpack_from("<HHI", data, offset + length - 8)
        if (end_type, end_flags, end_size) != (0, 0, 8):
            raise SystemExit("Multiboot2 end tag missing or malformed")
        print(f"Multiboot2 header verified at file offset {offset:#x}, length {length}")
        return
    raise SystemExit("No valid aligned Multiboot2 header found in first 32768 bytes")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify_multiboot2.py <kernel.elf>")
    verify(Path(sys.argv[1]))
