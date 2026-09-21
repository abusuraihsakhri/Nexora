# Phase 6 Acceptance Matrix

| Requirement | Implementation | Verification |
|---|---|---|
| Allocation latency baseline | `bench/bin/alloc_latency` | smoke test + suite |
| Graph scheduling overhead | `bench/bin/graph_sched` | two scheduler variants |
| Tensor lifetime memory peak | `bench/bin/lifetime_peak` | retain-all vs lifetime-aware |
| Zero-copy IPC | `bench/bin/zero_copy_ipc` | socket-copy vs shared-mem |
| Deadline tail latency | `bench/bin/deadline_tail` | FIFO vs EDF |
| Repeated execution | `scripts/run_suite.py` | raw JSONL + host metadata |
| Nexora ingestion | `scripts/ingest_nexora_serial.py` | strict schema/platform validation |
| Aggregation | `scripts/analyze.py` | CSV + JSON + Markdown + `comparability.json` |
| No fabricated comparison | report warning when Nexora data absent or parameters do not match | unit tests |
| Build/test gate | root `make test` | `-Werror`, unit and smoke tests |

Additional gate: Linux variants intended for direct comparison must use identical workload and measurement parameters at a given payload/problem size.
