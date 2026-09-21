# Phase 6 Execution — All Steps

## Step 1 — Freeze the comparison contract

Canonical schema: `nexora.bench.v1`. Each record has a benchmark ID, platform, variant, parameter object and metric object. Comparisons are valid only when benchmark and the complete parameter object match. The suite therefore uses identical iteration/warm-up settings for Linux variants at the same payload size.

## Step 2 — Build deterministic timing infrastructure

The Linux C benchmarks use `CLOCK_MONOTONIC_RAW` when available, explicit warm-up where appropriate, deterministic seeds, checksums to keep work observable, and machine-readable JSON output.

## Step 3 — Allocation latency baseline

`alloc_latency` measures allocation+touch+release latency for `malloc/free` and `mmap/munmap`, including median, p95 and p99.

## Step 4 — Graph scheduler baseline

`graph_sched` executes the same binary-tree DAG using a naive global ready scan or an explicit priority ready queue. It reports scheduler nanoseconds per node.

## Step 5 — Tensor lifetime peak baseline

`lifetime_peak` creates a deterministic tensor-lifetime trace and compares retain-until-end with final-consumer reclamation. The key metric is exact logical peak bytes rather than allocator-dependent RSS.

## Step 6 — Zero-copy IPC baseline

`zero_copy_ipc` compares payload handoff through an AF_UNIX socket with a shared `memfd` mapping plus a one-byte control message. `application_payload_copy_bytes_per_handoff` is explicitly an application-visible payload-copy accounting metric, not a claim about internal kernel copies.

## Step 7 — Deadline-tail baseline

`deadline_tail` simulates one non-preemptive server under FIFO and EDF using an identical deterministic arrival/service/deadline trace. It reports median/p95/p99 latency, deadline miss rate and simulator CPU cost.

## Step 8 — Automate repeated runs

`run_suite.py` builds the programs, captures host metadata (including visible process affinity when available), runs a fixed matrix repeatedly, optionally applies CPU affinity, and stores raw JSONL. Requested CPU pinning fails the benchmark command rather than silently continuing unpinned.

## Step 9 — Add Nexora telemetry ingestion and analysis

`ingest_nexora_serial.py` extracts only structurally valid `NEXORA_BENCH` records with finite numeric metrics from QEMU/serial logs. `analyze.py` uses the same path for Linux and Nexora records, emits a cross-platform parameter comparability audit, and refuses to manufacture absent Nexora data.

## Step 10 — Verification and release gate

`make test` runs Python unit tests, all five executable smoke tests, a one-repetition suite and report generation. The phase is accepted only if compilation uses `-Wall -Wextra -Werror`, tests pass, and the generated package contains no claimed Nexora speedup without matched Nexora telemetry. Native Linux timings must not be compared as architectural performance evidence against software-emulated Nexora timings.
