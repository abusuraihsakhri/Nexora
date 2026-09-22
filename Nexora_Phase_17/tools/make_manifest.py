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
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", nargs="?", default=".")
    parser.add_argument("-o", "--output", default="PHASE17_MANIFEST.json")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    output = Path(args.output)
    if not output.is_absolute():
        output = root / output

    entries = []
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(root)
        if excluded(rel) or rel.name in EXCLUDE_NAMES:
            continue
        entries.append(
            {
                "path": rel.as_posix(),
                "size": path.stat().st_size,
                "sha256": sha256(path),
            }
        )

    document = {
        "schema": 1,
        "project": "Nexora",
        "phase": 17,
        "algorithm": "sha256",
        "files": entries,
    }
    output.write_text(
        json.dumps(document, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"wrote {output} ({len(entries)} files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
