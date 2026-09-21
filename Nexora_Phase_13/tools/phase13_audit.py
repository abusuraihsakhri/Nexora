#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
REQUIRED = [
    "include/ai/reliability.h",
    "src/ai/reliability.c",
    "include/ai/observability.h",
    "src/ai/observability.c",
    "include/ai/obs_reliability.h",
    "src/ai/obs_reliability.c",
    "tests/test_phase12.c",
    "tests/test_policy_edges.c",
    "tests/test_phase13.c",
    "tests/test_phase13_edges.c",
    "docs/PHASE13_ARCHITECTURE.md",
    "docs/PHASE13_EXIT_CRITERIA.md",
    "docs/CROSS_PHASE_CONTRACTS.md",
]
FORBIDDEN = re.compile(
    r"\b(?:malloc|calloc|realloc|free|fopen|fclose|printf|fprintf|sprintf|snprintf|memcpy|memset|strlen|strcpy|pthread_[A-Za-z0-9_]*)\b"
)


def sha256(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def main() -> int:
    failures: list[str] = []
    for rel in REQUIRED:
        if not (ROOT / rel).is_file():
            failures.append(f"missing required file: {rel}")

    core_files = sorted((ROOT / "src" / "ai").glob("*.c"))
    for path in core_files:
        text = path.read_text(encoding="utf-8")
        match = FORBIDDEN.search(text)
        if match:
            failures.append(f"forbidden hosted/runtime symbol in {path.relative_to(ROOT)}: {match.group(0)}")

    if failures:
        for failure in failures:
            print(f"AUDIT FAIL: {failure}", file=sys.stderr)
        return 1

    print("Phase 13 package audit: PASS")
    for path in core_files:
        print(f"  {sha256(path)}  {path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
