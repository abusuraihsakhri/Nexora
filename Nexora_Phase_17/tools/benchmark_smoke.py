#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json, statistics, time


def workload(iterations: int) -> str:
    h = hashlib.sha256()
    for i in range(iterations):
        h.update(i.to_bytes(8, "little"))
        h.update(((i * 0x9E3779B97F4A7C15) & ((1 << 64) - 1)).to_bytes(8, "little"))
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--iterations", type=int, default=50_000)
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--json", action="store_true")
    ns = ap.parse_args()
    digests, times = [], []
    for _ in range(ns.runs):
        t0 = time.perf_counter_ns()
        digests.append(workload(ns.iterations))
        times.append((time.perf_counter_ns() - t0) / 1e6)
    deterministic = len(set(digests)) == 1
    result = {
        "iterations": ns.iterations,
        "runs": ns.runs,
        "deterministic": deterministic,
        "digest": digests[0],
        "median_ms": statistics.median(times),
        "min_ms": min(times),
        "max_ms": max(times),
    }
    if ns.json: print(json.dumps(result, sort_keys=True))
    else:
        print("benchmark smoke:", result)
    return 0 if deterministic else 2

if __name__ == "__main__": raise SystemExit(main())
