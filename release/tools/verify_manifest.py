#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json
from pathlib import Path

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
    bad = []
    for e in doc.get("files", []):
        p = root / e["path"]
        if not p.is_file(): bad.append(f"missing: {e['path']}"); continue
        if p.stat().st_size != e["size"]: bad.append(f"size: {e['path']}"); continue
        if sha256(p) != e["sha256"]: bad.append(f"hash: {e['path']}")
    if bad:
        print("manifest verification FAILED")
        for x in bad: print(" -", x)
        return 1
    print(f"manifest verification passed ({len(doc.get('files', []))} files)")
    return 0

if __name__ == "__main__": raise SystemExit(main())
