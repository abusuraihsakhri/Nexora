# Phase 11.1 Validation Plan

Run the complete gate:

```bash
make phase11-crosscheck
```

The script is the release gate; it reruns the checks below rather than relying on previously reported results.

## Gate A — Deterministic + property host tests

```bash
make phase11-test
```

Deterministic coverage includes:

- deny by default;
- exact subject/resource matching;
- rights isolation;
- expiry;
- per-request byte ceilings;
- repeated requests demonstrating non-cumulative byte semantics;
- device-demand ceilings;
- explicit deny precedence;
- successful delegation attenuation;
- rights/resource/lifetime/byte widening rejection;
- cascading revocation;
- invalid policy-effect rejection and invariant detection;
- rejection of `AI_POLICY_ANY_ID` as a concrete request object;
- explicit inactive-slot reclamation;
- full-table pressure followed by revoked-slot reuse;
- epoch invalidation plus complete stale-slot reuse;
- invariant validation;
- ordered audit retrieval.

`tests/phase11_property_test.c` independently generates 5,000 randomized delegation attempts and compares acceptance against a separately stated attenuation predicate. Every accepted child is rechecked for rights/resource/time/byte/quota monotonicity and passed through `ai_policy_validate()`.

The property test also forces more than 256 audit writes and verifies strict logical sequence order after ring-buffer wraparound.

## Gate B — Sanitizers

```bash
make phase11-sanitize
```

Both host suites are compiled and executed with AddressSanitizer and UndefinedBehaviorSanitizer.

## Gate C — Static analysis

The cross-check compiles `src/ai/policy.c` using GCC `-fanalyzer` under `-Werror`.

## Gate D — Freestanding compile

`src/ai/policy.c` must compile with the kernel freestanding flags and no libc dependency.

## Gate E — Clean kernel link

The phase gate removes previous build output and links the full x86-64 kernel ELF with the policy module and demo.

## Gate F — Dependency/source regression checks

The linked ELF is checked for common unexpected libc references. Source checks confirm that Phase 11.1 hardening hooks are present: effect invariant, wildcard-request validation, reclamation API, management trust contract, synchronization contract, and per-request byte semantics.

## Gate G — Release checksums

`sha256sum -c SHA256SUMS` must pass for every release file listed in the package checksum manifest.

## Gate H — Multiboot/QEMU

When GRUB tooling is available, the kernel ELF is checked with `grub-file`. QEMU/GRUB ISO tooling is reported separately because an interactive boot is environment-dependent.

When QEMU + GRUB are available locally:

```bash
make
make run
```

Expected serial decisions include tensor read allow, tensor write deny, GPU demand above ceiling deny, GPU demand below ceiling allow, attenuated delegation allow, privilege amplification deny, delegated authority after parent revocation deny, and invariant validation pass.

## Phase 10 enforcement-point validation

Phase 10 privileged paths should call `ai_policy_authorize()` immediately before tensor/model/device/network/remote/collective commitments using kernel-derived identity and demand fields. On SMP systems, the policy and resource state must be synchronized so revoke/epoch mutation cannot race the authorized commitment.
