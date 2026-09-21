# Nexora — Phase 17

## Release-candidate hardening, observability, and reproducibility

Phase 17 converts the validated Phase 16 architecture into a release-candidate discipline. It adds low-level health reporting, trace capture, watchdog supervision, deterministic smoke benchmarking, integrity manifests, and a single release gate.

### Why this phase now

After integration and validation, the next engineering risk is not another feature layer; it is silent regression, non-reproducible builds, poor post-failure visibility, and undefined release criteria. Phase 17 addresses those risks without changing core scheduler/allocator/IPC/security semantics.

### Kernel-facing modules

- `health`: fixed-capacity component registry with OK/DEGRADED/FAILED aggregation.
- `trace`: 1,024-event structured ring buffer with monotonic sequence numbers.
- `watchdog`: heartbeat supervision driven by a caller-supplied monotonic clock.
- `build_info`: immutable build identity for diagnostics.

The modules intentionally perform no dynamic allocation and create no threads.

### Host-side release tooling

- `tools/benchmark_smoke.py`: deterministic workload check.
- `tools/make_manifest.py`: SHA-256 source/release manifest.
- `tools/verify_manifest.py`: independent manifest verification.
- `tools/release_gate.py`: runs compile/tests + benchmark + integrity checks.

### Run

```sh
make gate
```

or:

```sh
./tests/run_tests.sh
python3 tools/release_gate.py
```

### Phase 17 definition of done

The phase is complete when the standalone gate passes and the modules are wired into the Phase 16 component lifecycle. See `docs/PHASE17_ACCEPTANCE.md` and `PHASE17_INTEGRATION.md`.
