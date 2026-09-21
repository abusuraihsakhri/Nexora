# Nexora — Phase 10

**Resilience, Recovery & Production Hardening — cross-checked revision**

Phase 10 adds failure detection and escalation, checkpoint metadata, capability-gated recovery, watchdog behavior, audit logging and distributed recovery-target selection on top of the distributed execution layer built in Phase 9.

## Contents

- `include/nexora/phase10/` — public/internal Phase 10 interfaces
- `kernel/phase10/` — reference kernel-side implementation
- `tests/test_phase10.c` — deterministic unit/integration tests
- `tests/test_phase10_adversarial.c` — adversarial/security/boundary tests
- `docs/PHASE_10_ARCHITECTURE.md` — architecture and invariants
- `docs/INTEGRATION_GUIDE.md` — merge points into Phases 1–9
- `docs/PHASE_10_ACCEPTANCE.md` — completion gates
- `CROSSCHECK_REPORT.md` — independent cross-check findings and corrections
- `tools/phase10_sanity.sh` — strict local suite
- `tools/phase10_crosscheck.sh` — manifest + Make + CMake/CTest verification

## Verify

```sh
make crosscheck
```

or:

```sh
./tools/phase10_sanity.sh
```

For a full archive verification after extraction:

```sh
./tools/phase10_crosscheck.sh
```

## Important integration boundary

This package is self-contained enough to compile and test independently, but the final whole-kernel integration must use Nexora's existing clock, synchronization, scheduler, checkpoint payload, accelerator and Phase 9 distributed migration mechanisms. The reference layer intentionally does not fake those platform mechanisms.
