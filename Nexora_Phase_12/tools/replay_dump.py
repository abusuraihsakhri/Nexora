#!/usr/bin/env python3
"""Minimal host-side decoder for exported Phase 12 replay events.

Input format: CSV lines with
sequence,timestamp_ns,kind,actor_id,object_id,arg0,arg1

The kernel does not depend on Python. This tool is only for offline inspection.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

EVENTS = {
    1: "BOOT",
    2: "DOMAIN_REGISTER",
    3: "HEARTBEAT",
    4: "FAULT",
    5: "HEALTH_TRANSITION",
    6: "SCHED_PICK",
    7: "RESOURCE_ASSIGN",
    8: "WORK_START",
    9: "WORK_COMPLETE",
    10: "WORK_FAIL",
    11: "RECOVERY_DECISION",
    12: "CHECKPOINT",
    13: "SAFE_MODE_ENTER",
    14: "SAFE_MODE_EXIT",
}


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Dump Nexora Phase 12 replay events")
    p.add_argument("csv", type=Path)
    return p.parse_args()


def main() -> int:
    args = parse_args()
    with args.csv.open(newline="", encoding="utf-8") as fh:
        reader = csv.reader(fh)
        for row_no, row in enumerate(reader, start=1):
            if not row or row[0].startswith("#"):
                continue
            if len(row) != 7:
                raise SystemExit(f"line {row_no}: expected 7 columns, got {len(row)}")
            seq, ts, kind, actor, obj, arg0, arg1 = (int(x, 0) for x in row)
            name = EVENTS.get(kind, f"UNKNOWN({kind})")
            print(
                f"seq={seq:6d} t={ts:12d} {name:20s} "
                f"actor={actor} object={obj} arg0={arg0} arg1={arg1}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
