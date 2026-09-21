#!/usr/bin/env python3
"""Generate deterministic example Phase 16 benchmark traces (40 runs/scenario)."""
from __future__ import annotations

import argparse
import csv
import random
from pathlib import Path

FIELDS = [
    "run_id", "test", "success", "latency_ns", "peak_memory_bytes", "bytes_copied",
    "isolation_violations", "unrecovered_faults", "telemetry_drops",
]
TESTS = [
    "boot",
    "scheduler",
    "allocator",
    "zero_copy_shared_tensor",
    "capability_isolation",
    "fault_recovery",
    "distributed_remote",
    "telemetry_pressure",
]
BASE_NS = {
    "boot": 2_500_000,
    "scheduler": 180_000,
    "allocator": 60_000,
    "zero_copy_shared_tensor": 90_000,
    "capability_isolation": 75_000,
    "fault_recovery": 220_000,
    "distributed_remote": 480_000,
    "telemetry_pressure": 140_000,
}
BASE_MEM_MIB = {
    "boot": 72,
    "scheduler": 40,
    "allocator": 64,
    "zero_copy_shared_tensor": 48,
    "capability_isolation": 42,
    "fault_recovery": 44,
    "distributed_remote": 68,
    "telemetry_pressure": 52,
}


def write(path: Path, seed: int, factor: float) -> None:
    rng = random.Random(seed)
    rows: list[list[object]] = []
    for test in TESTS:
        for i in range(40):
            latency = int(BASE_NS[test] * factor * (1.0 + rng.uniform(-0.025, 0.025)))
            peak = int(BASE_MEM_MIB[test] * 1024 * 1024 * factor)
            payload_copy = 0 if test == "zero_copy_shared_tensor" else 4096
            rows.append([
                f"{test}-{i:03d}", test, 1, latency, peak, payload_copy, 0, 0, 0,
            ])
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(FIELDS)
        writer.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-dir", type=Path, default=Path("."))
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    write(args.out_dir / "baseline.csv", 1601, 1.0)
    write(args.out_dir / "candidate.csv", 1602, 1.02)


if __name__ == "__main__":
    main()
