# Nexora historical phase snapshots

The `Nexora_Phase_XX/` directories are retained as historical research snapshots and provenance material. They are **not** the active build surface.

Current development lives in:

- `kernel/` — authoritative Nexora-RK source tree;
- `bench/` — benchmark/event contract and regression tooling;
- `qualification/` — release qualification, fault injection, and telemetry validation;
- `release/` — hardening, observability tests, manifests, and release-gate tooling.

The phase snapshots may contain older interfaces, reports, or documentation that no longer match the current kernel. Git history is the canonical record of how those snapshots evolved.

## Provenance rule

Historical gaps are documented rather than reconstructed from inference. In particular, Phase 08 was not present in the recovered repository and is represented by an explicit provenance marker instead of fabricated code.
