# Nexora Phase 11.1 Package

This package is the corrected Phase 11 security/policy reference implementation for Nexora. It is self-contained for host testing and can also be compiled into the original Nexora/AIKernel starter tree.

The Phase 11.1 hardening pass adds strict effect validation, rejects wildcard sentinels as concrete request IDs, reclaims inactive grant slots safely, makes management-operation trust semantics explicit, documents external synchronization/TOCTOU requirements, and turns sanitizer/static-analysis checks into part of the reproducible phase gate.

Primary files:

```text
PHASE11.md
PHASE11_1_CORRECTIONS.md
include/ai/policy.h
include/ai/policy_demo.h
src/ai/policy.c
src/ai/policy_demo.c
tests/phase11_policy_test.c
tests/phase11_property_test.c
docs/PHASE11_SECURITY_MODEL.md
docs/PHASE11_THREAT_MODEL.md
docs/PHASE11_VALIDATION.md
docs/PHASE11_INTEGRATION.md
spec/POLICY_SEMANTICS.md
scripts/phase11_crosscheck.sh
```

Run the complete phase gate:

```bash
make phase11-crosscheck
```

The gate reruns host tests, ASan/UBSan, GCC static analysis, the freestanding compile, a clean full kernel link, dependency/regression checks, and package checksum verification. GRUB/QEMU checks remain conditional on local tooling availability.
