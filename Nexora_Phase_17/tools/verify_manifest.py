#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

EXCLUDE_DIRS = {".git", "build", "dist", "__pycache__"}
EXCLUDE_NAMES = {"PHASE17_MANIFEST.json"}


def excluded(rel: Path) -> bool:
    return any(part in EXCLUDE_DIRS or part.startswith("build-") for part in rel.parts)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("manifest", nargs="?", default="PHASE17_MANIFEST.json")
    ns = ap.parse_args()
    manifest = Path(ns.manifest).resolve()
    root = manifest.parent
    doc = json.loads(manifest.read_text(encoding="utf-8"))

    entries = doc.get("files", [])
    expected = {e["path"]: e for e in entries}
    actual = {
        p.relative_to(root).as_posix(): p
        for p in root.rglob("*")
        if p.is_file()
        and not excluded(p.relative_to(root))
        and p.name not in EXCLUDE_NAMES
    }

    bad: list[str] = []
    for path in sorted(set(expected) - set(actual)):
        bad.append(f"missing: {path}")
    for path in sorted(set(actual) - set(expected)):
        bad.append(f"unlisted: {path}")

    for path in sorted(set(expected) & set(actual)):
        e = expected[path]
        p = actual[path]
        if p.stat().st_size != e["size"]:
            bad.append(f"size: {path}")
            continue
        if sha256(p) != e["sha256"]:
            bad.append(f"hash: {path}")

    if bad:
        print("manifest verification FAILED")
        for item in bad:
            print(" -", item)
        return 1

    print(f"manifest verification passed ({len(entries)} files, exact set)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
