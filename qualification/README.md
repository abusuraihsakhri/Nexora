# Nexora Phase 16

**Performance Qualification & Release Hardening** for the Phase 15 Nexora/AIKernel tree.

This package is an additive overlay because the actual Phase 15 ZIP is not available in
this conversation. It therefore avoids guessing or rewriting Phase 15 scheduler, memory,
security, distributed, or syscall internals.

## Contents

- `include/ai/telemetry.h`, `src/ai/telemetry.c` — bounded event ring buffer
- `include/ai/fault_injection.h`, `src/ai/fault_injection.c` — deterministic failures
- `include/ai/release_health.h`, `src/ai/release_health.c` — release health registry
- `host/phase16_validate.py` — baseline/candidate release-gate validator
- `host/phase16_tracegen.py` — deterministic example data covering mandatory scenarios
- `config/phase16_gates.json` — default gates and required scenario names
- `tests/test_phase16.c` — C unit tests for the freestanding modules
- `tests/test_validator.py` — adversarial/unit tests for the release validator
- `scripts/apply_phase16.py` — preflighted additive copier for a Phase 15 source tree
- `docs/` — qualification plan, benchmark protocol, and release gates
- `CROSS_CHECK_REPORT.md` — post-build code/ZIP audit and fixes

## Verify this package

```bash
make -f Makefile.phase16 test
make -f Makefile.phase16 demo
make -f Makefile.phase16 sanitize
```

Expected high-level result:

```text
phase16 unit tests: PASS
Ran 7 tests ... OK
Nexora Phase 16 release gates: PASS
phase16 unit tests: PASS
```

`sanitize` requires a compiler/runtime with AddressSanitizer and UndefinedBehaviorSanitizer.

## Release-gate behavior

The validator requires both baseline and candidate datasets to:

- contain all configured mandatory scenarios;
- contain at least the configured number of unique runs per scenario;
- meet the success-rate gate;
- have zero blocking isolation/recovery/telemetry errors under the default policy;
- satisfy zero-copy payload constraints for explicitly configured zero-copy scenarios.

Performance regression checks are then calculated per scenario for median latency, p99
latency, and peak memory. The baseline and candidate scenario sets must match.

## Apply to the real Phase 15 tree

Preview changes without modifying the target:

```bash
python3 scripts/apply_phase16.py --dry-run /path/to/Nexora_Phase15
```

Apply when no destination conflicts exist:

```bash
python3 scripts/apply_phase16.py /path/to/Nexora_Phase15
```

The copier performs a full conflict preflight before writing anything. If reviewed
Phase 16 files already exist, use `--force` explicitly.

Then add these C sources to the kernel build if the build system does not discover
`src/ai/*.c` automatically:

```text
src/ai/telemetry.c
src/ai/fault_injection.c
src/ai/release_health.c
```

Instrument reviewed subsystem boundaries using `ai_telemetry_emit()` and
`ai_fault_should_fail()`. Do not put expensive formatting or serial output in the hot
path.

## Integration limitation

The overlay has been freestanding-compiled and linked into the original x86-64
AIKernel starter tree. This is a compatibility check, not a substitute for a real
Phase 15 -> Phase 16 merge validation. Once the actual Phase 15 tree is supplied, its
existing integration suite and concrete scheduler/memory/security/distributed paths
must be exercised with Phase 16 instrumentation.
