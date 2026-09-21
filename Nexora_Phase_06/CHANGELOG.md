# Changelog

## 0.6.1 — cross-check hardening

- matched allocation benchmark iteration/warm-up parameters across Linux variants
- fixed zero-copy child-error detection
- made requested CPU affinity fail closed instead of silently running unpinned
- added strict finite-numeric telemetry validation
- added cross-platform parameter comparability audit and tests
- documented virtualization/timing parity and pre-established shared-handle semantics


## 0.6.0 — Phase 6

- Added five Linux host-side benchmark families required by the Nexora roadmap.
- Added `nexora.bench.v1` machine-readable telemetry contract.
- Added repeated-suite orchestration with host metadata capture and optional CPU affinity.
- Added Nexora serial telemetry ingestion.
- Added aggregate JSON, CSV and Markdown reporting.
- Added strict build, unit and executable smoke-test gate.
- Added Phase-5 instrumentation integration guidance.
