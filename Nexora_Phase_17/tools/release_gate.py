#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


def run(cmd: list[str], cwd: Path) -> None:
    print("+", " ".join(map(str, cmd)))
    subprocess.run(cmd, cwd=cwd, check=True)


def main() -> int:
    ap = argparse.ArgumentParser(description="Nexora Phase 17 release-candidate gate")
    ap.add_argument("--root", default=str(Path(__file__).resolve().parents[1]))
    ap.add_argument("--skip-build", action="store_true")
    ns = ap.parse_args()
    root = Path(ns.root).resolve()
    cfg = json.loads((root / "configs/release_gate.json").read_text(encoding="utf-8"))

    required = [root / p for p in cfg["required_files"]]
    missing = [str(p.relative_to(root)) for p in required if not p.exists()]
    if missing:
        print("missing required files:", *missing, sep="\n - ")
        return 2

    # Verify the committed integrity record before executing project code.
    # Do not regenerate it here: generating and immediately verifying the same
    # manifest cannot detect an unintended source change.
    run(
        [
            sys.executable,
            str(root / "tools/verify_manifest.py"),
            str(root / "PHASE17_MANIFEST.json"),
        ],
        root,
    )

    if not ns.skip_build:
        if shutil.which(os.environ.get("CC", "cc")) is None:
            print("C compiler unavailable")
            return 3
        run(["sh", str(root / "tests/run_tests.sh")], root)

    run(
        [
            sys.executable,
            str(root / "tools/benchmark_smoke.py"),
            "--runs",
            "3",
            "--json",
        ],
        root,
    )
    print("PHASE 17 RELEASE GATE: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
