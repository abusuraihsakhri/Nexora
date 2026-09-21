# Phase 13 Exit Criteria

Phase 13 is complete when all of the following are true in the integrated Nexora tree:

- [x] Metric registry is fixed-capacity and heap-free.
- [x] Counters, gauges, and histograms are supported.
- [x] Histogram bounds are validated and observations are bounded.
- [x] Counter/histogram accumulation cannot silently wrap.
- [x] Metric scopes cover system, scheduler, device, agent, remote node, network path, work graph, tensor, and service domains.
- [x] Public/operator/sensitive visibility labels exist.
- [x] Clearance-aware metric reads and trace export enforce those labels.
- [x] Trace storage is bounded and reports overwrite count.
- [x] Trace records carry correlation and span identifiers.
- [x] Active spans are bounded and capacity exhaustion is explicit.
- [x] Span duration can feed a histogram metric.
- [x] Clock regression cannot underflow span duration silently.
- [x] Diagnostic snapshots contain trace/metric checksums and loss/rejection counters.
- [x] Phase 12 reliability events can be mirrored without modifying Phase 12 core behavior.
- [x] Reliability-journal overwrite before mirroring produces an explicit source-gap record.
- [x] Phase 12 regression tests remain green.
- [x] Phase 13 unit and edge tests pass under strict warnings.
- [x] Core sources compile with `-ffreestanding -fno-builtin -fno-stack-protector`.
- [x] AddressSanitizer/UBSan validation passes.
- [x] Combined Phase 12 + 13 freestanding object has no unresolved runtime symbols.

## Non-goals

Intentionally deferred:

- persistent filesystem logging;
- network telemetry transport;
- OpenTelemetry wire compatibility;
- eBPF-style programmable probes;
- per-CPU lock-free tracing;
- production crash-dump storage;
- tensor/model/user-memory payload capture;
- autonomous policy changes driven directly by metrics;
- remote distributed trace consensus.
