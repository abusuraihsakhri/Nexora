#!/usr/bin/env python3
"""Generate deterministic Phase 15 example benchmark events."""
from __future__ import annotations
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "fixtures" / "sample_run.jsonl"

specs = [
    ("scheduler.dispatch", "latency_ns", "ns", "graph", "mixed", 800),
    ("scheduler.tail", "latency_ns", "ns", "deadline", "mixed", 1400),
    ("tensor.alloc", "latency_ns", "ns", "lifetime-aware", "4KiB", 500),
    ("tensor.peak", "bytes", "bytes", "lifetime-aware", "dag-small", 1024 * 1024),
    ("tensor.reclaim", "latency_ns", "ns", "final-consumer", "dag-small", 350),
    ("ipc.copy", "latency_ns", "ns", "shared-handle", "1MiB", 1200),
    ("ipc.copy", "bytes_copied", "bytes", "shared-handle", "1MiB", 0),
    ("capability.check", "latency_ns", "ns", "allow", "tensor-read", 90),
    ("distributed.transfer", "latency_ns", "ns", "simulated-remote", "64KiB", 3200),
]

with OUT.open("w", encoding="utf-8") as fh:
    for bench, metric, unit, variant, workload, base in specs:
        for i in range(40):
            # deterministic small spread; no RNG dependency
            jitter = ((i * 17 + len(bench)) % 21) - 10
            value = max(0, base + jitter)
            event = {
                "schema": "nexora.bench.v1",
                "run_id": "phase15-sample",
                "benchmark": bench,
                "metric": metric,
                "value": value,
                "unit": unit,
                "iteration": i,
                "variant": variant,
                "workload": workload,
                "phase": "15"
            }
            fh.write(json.dumps(event, separators=(",", ":")) + "\n")
print(OUT)
