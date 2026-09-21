# Phase 16 Release Gates

The machine-readable defaults are in `config/phase16_gates.json`.

## Required scenarios

Both baseline and candidate qualification datasets must include every configured
mandatory scenario. The default set is:

- `boot`
- `scheduler`
- `allocator`
- `zero_copy_shared_tensor`
- `capability_isolation`
- `fault_recovery`
- `distributed_remote`
- `telemetry_pressure`

Additional scenarios are allowed, but the baseline and candidate scenario sets must
match exactly.

## Blocking gates

For both baseline and candidate where applicable:

- unique run count per scenario >= 30;
- success rate = 1.0;
- isolation violations = 0;
- unrecovered mandatory injected faults = 0;
- telemetry drops = 0 during qualification;
- configured zero-copy scenarios report copied payload bytes = 0.

Candidate-vs-baseline performance gates are:

- median latency regression <= 5%;
- p99 latency regression <= 10%;
- peak-memory regression <= 5%.

A failed blocking gate means the candidate is not Phase-16-qualified. The correct
response is root-cause analysis or an explicitly reviewed gate/baseline revision,
not suppressing the measurement.

## Dataset integrity

`run_id` must be nonblank and unique within each scenario. This prevents repeated
copies of one observation from satisfying the minimum-run gate. Required scenario
names and zero-copy scenario names are explicit configuration fields rather than
being inferred from substrings.

## Non-blocking health warnings

Project-specific checks may be registered as non-critical health checks. WARN can be
used for experimental devices or optional remote backends, but WARN must never mask a
critical isolation, data-integrity, or recovery failure.
