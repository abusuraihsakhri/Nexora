#!/usr/bin/env python3
"""Copy Phase 16 additive kernel files into a Nexora/AIKernel source tree.

The script intentionally refuses to rewrite kmain, scheduler, memory, security,
or distributed subsystems because Phase 15 layouts may differ. It performs a
full conflict preflight before copying anything, so a rejected invocation does
not leave a partially modified source tree.
"""
from __future__ import annotations

import argparse
import shutil
from pathlib import Path

FILES = [
    ("include/ai/telemetry.h", "include/ai/telemetry.h"),
    ("include/ai/fault_injection.h", "include/ai/fault_injection.h"),
    ("include/ai/release_health.h", "include/ai/release_health.h"),
    ("src/ai/telemetry.c", "src/ai/telemetry.c"),
    ("src/ai/fault_injection.c", "src/ai/fault_injection.c"),
    ("src/ai/release_health.c", "src/ai/release_health.c"),
]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("target", type=Path)
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    src_root = Path(__file__).resolve().parents[1]
    target = args.target.resolve()
    if not target.is_dir() or not (target / "include").is_dir() or not (target / "src").is_dir():
        raise SystemExit("target does not look like a Nexora/AIKernel source tree")

    plan: list[tuple[Path, Path, str]] = []
    conflicts: list[Path] = []
    for src_rel, dst_rel in FILES:
        src = src_root / src_rel
        dst = target / dst_rel
        if not src.is_file():
            raise SystemExit(f"Phase 16 package is missing required source: {src_rel}")
        action = "overwrite" if dst.exists() else "copy"
        if dst.exists() and not args.force:
            conflicts.append(dst)
        plan.append((src, dst, action))

    if conflicts:
        joined = "\n  ".join(str(path) for path in conflicts)
        raise SystemExit(
            "refusing to modify target because destination files already exist; "
            "no files were copied. Use --force after review:\n  " + joined
        )

    for src, dst, action in plan:
        print(f"{action}: {dst.relative_to(target)}")
        if args.dry_run:
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)

    if args.dry_run:
        print("\nDry run only; target was not modified.")
    print("\nIf your build lists C sources explicitly, add:")
    print("  src/ai/telemetry.c")
    print("  src/ai/fault_injection.c")
    print("  src/ai/release_health.c")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
