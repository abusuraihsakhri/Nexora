# Nexora Phase 6 Cross-Check Report

Cross-check date: 2026-09-20
Release after cross-check: **0.6.1**

## Scope

The audit compared:

1. the Phase-6 implementation against the AIKernel/Nexora starter roadmap's Milestone 6;
2. source code against Phase-6 documentation and acceptance claims;
3. benchmark-pair parameter parity;
4. generated telemetry against the `nexora.bench.v1` contract;
5. source tree against packaged archive content;
6. executable behavior under normal builds, unit tests, smoke tests and sanitizers.

The cumulative Phase-5 source tree is not available as a mounted/current Project file in this conversation. Therefore the audit can verify the Phase-6 overlay and its contract against the available starter roadmap, but it cannot truthfully certify a source-level Phase-5 ABI merge.

## Findings discovered and corrected

### 1. Allocation benchmark parameter mismatch — FIXED

The original Phase-6 suite used different iteration and warm-up counts for `malloc` and `mmap`. That contradicted the documented rule that directly compared variants must use identical parameter objects.

The quick and full matrices now use identical iteration/warm-up settings at each allocation size. Automated validation verifies equality of the complete parameter-set collections for all five Linux baseline pairs.

### 2. Zero-copy child failures could be missed — FIXED

The IPC child previously used its checksum as the process exit code, while the parent checked only `WIFEXITED`. A child failure using an ordinary nonzero `_exit()` code could therefore be indistinguishable from a successful checksum exit.

The child now exits `0` on success and the parent requires both `WIFEXITED(status)` and `WEXITSTATUS(status) == 0`.

### 3. Requested CPU affinity could silently fail — FIXED

The orchestrator previously swallowed `sched_setaffinity` failure. A run could therefore be labelled with a requested CPU while actually running unpinned.

Affinity requests now fail closed. An intentionally invalid CPU ID was tested and the run correctly failed instead of continuing.

### 4. Cross-platform parameter mismatch was not surfaced strongly enough — FIXED

The analyzer grouped results by parameters but did not explicitly warn when both Linux and Nexora data existed without any identical benchmark/parameter group.

The analyzer now emits `comparability.json`, lists matched cross-platform parameter groups in the Markdown report, and warns when both platforms are present but no valid matched group exists.

### 5. Synthetic example telemetry could be mistaken for evidence — FIXED

The bundled Nexora serial example is now marked `"fixture": true`. The parser still accepts it for smoke testing, but the analyzer rejects fixture telemetry as performance evidence.

### 6. Execution-environment parity needed an explicit gate — DOCUMENTED

Native Linux timings must not be compared with a Nexora kernel running under software-emulated QEMU/TCG and then attributed to OS architecture. The methodology now requires comparable execution mode/hardware, clock calibration, CPU affinity and power-state documentation. Functional/algorithmic comparisons may still be performed when timing parity is absent, but they must be labelled accordingly.

### 7. Zero-copy setup semantics needed alignment — DOCUMENTED

The Linux shared-memory benchmark establishes the mapping before the timed handoff loop. Nexora must likewise establish/delegate the shared tensor handle before the timed loop for this benchmark. Handle/delegation setup cost is a separate experiment and should use an equivalent Linux setup path such as descriptor passing.

## Roadmap alignment

The available starter roadmap defines Milestone 6 as a host-side comparison harness for:

- allocation latency;
- graph scheduling overhead;
- tensor-lifetime memory peak;
- zero-copy IPC;
- deadline tail latency.

All five benchmark families are implemented in Phase 6. The package therefore reaches the Milestone-6 implementation objective.

The starter roadmap also says to prove an architectural advantage before expanding the real-device path. Phase 6 does **not** yet prove such an advantage, because real matched Nexora kernel telemetry is not present. This is consistent across the README, execution guide, methodology, analyzer report and validation report.

## Final validation after corrections

- Release build with `-std=c11 -O2 -Wall -Wextra -Werror`: **PASS**.
- Python syntax/import check: **PASS**.
- Python unit tests: **PASS (6/6)**.
- Executable smoke suite: **PASS**.
- AddressSanitizer + UndefinedBehaviorSanitizer smoke executions: **PASS**.
- Repeated quick suite: **PASS — 30 records** (3 repetitions × 10 commands).
- Full benchmark matrix: **PASS — 14 records**.
- Schema and finite-numeric metric validation: **PASS**.
- Linux pair parameter parity across all five benchmark families: **PASS**.
- Synthetic fixture rejection by analysis path: **PASS**.
- Invalid CPU-affinity fail-closed test: **PASS**.
- Analysis outputs: `summary.json`, `summary.csv`, `comparability.json`, `report.md`: **PASS**.

## What the Linux validation data show

These are sanity observations from one validation environment, not architectural claims about Nexora:

- explicit ready-queue scheduling has much lower scheduler bookkeeping cost than repeated global scanning for the tested binary-tree DAG;
- final-consumer-style lifetime reclamation drastically reduces logical peak live tensor bytes relative to retaining every tensor until the end of the trace;
- shared-memory handoff records zero application payload-copy bytes after setup, while the socket-copy variant copies the payload by construction;
- EDF reduces deadline misses in the fixed trace used here, but does not dominate FIFO on every latency statistic and has higher simulator bookkeeping cost in this implementation;
- `malloc/free` and `mmap/munmap` represent materially different allocation paths and should be treated as reference baselines, not interchangeable Linux primitives.

The fact that the internal baselines exhibit trade-offs rather than a single universally dominant variant is useful: the harness is not hard-coded to manufacture a predetermined result.

## Cross-check conclusion

**Phase 6 v0.6.1 is internally consistent with the available Nexora/AIKernel Milestone-6 roadmap and passes the executable validation gates after the corrections above.**

**No Nexora-vs-Linux speed, latency or memory-efficiency conclusion is established yet.** The next valid evidence step is to instrument the actual cumulative Phase-5 kernel, emit matched non-fixture `nexora.bench.v1` records, and run both environments under documented equivalent timing conditions.
