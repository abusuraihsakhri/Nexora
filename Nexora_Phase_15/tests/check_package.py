#!/usr/bin/env python3
from pathlib import Path
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
required = [
    "README.md",
    "docs/PHASE15_SPEC.md",
    "docs/INTEGRATION.md",
    "schemas/bench_event.schema.json",
    "config/thresholds.json",
    "tools/nxbench.py",
    "integration/kernel/nexora_bench.h",
    "integration/kernel/nexora_bench.c",
]
missing = [p for p in required if not (ROOT / p).is_file()]
if missing:
    raise SystemExit("missing required files: " + ", ".join(missing))

thresholds = json.loads((ROOT / "config/thresholds.json").read_text())
if thresholds.get("version") != 1 or not thresholds.get("rules"):
    raise SystemExit("invalid thresholds configuration")

# Syntax-compile the kernel adapter as a freestanding-compatible object on the host.
out = ROOT / "results" / "nexora_bench.o"
subprocess.run([
    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-ffreestanding", "-fno-builtin",
    "-I", str(ROOT / "integration/kernel"), "-c", str(ROOT / "integration/kernel/nexora_bench.c"), "-o", str(out)
], check=True)
print("package checks: PASS")
