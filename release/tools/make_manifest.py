#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json
from pathlib import Path

EXCLUDE_DIRS = {".git", "build", "dist", "__pycache__"}

def excluded(rel: Path) -> bool:
    return any(part in EXCLUDE_DIRS or part.startswith("build-") for part in rel.parts)
EXCLUDE_NAMES = {"PHASE17_MANIFEST.json"}

def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("root", nargs="?", default=".")
    ap.add_argument("-o", "--output", default="PHASE17_MANIFEST.json")
    ns = ap.parse_args()
    root = Path(ns.root).resolve()
    out = Path(ns.output)
    if not out.is_absolute(): out = root / out
    entries = []
    for p in sorted(root.rglob("*")):
        if not p.is_file(): continue
        rel = p.relative_to(root)
        if excluded(rel): continue
        if rel.name in EXCLUDE_NAMES: continue
        entries.append({"path": rel.as_posix(), "size": p.stat().st_size, "sha256": sha256(p)})
    doc = {"schema": 1, "project": "Nexora", "phase": 17, "algorithm": "sha256", "files": entries}
    out.write_text(json.dumps(doc, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {out} ({len(entries)} files)")
    return 0

if __name__ == "__main__": raise SystemExit(main())
