# Phase 9 deep cross-check findings

## Scope checked

The cross-check covers the packaged Phase 9 module, documentation, host tests, the baseline starter ABI fixture, and the Milestone 9 roadmap contract. The exact evolved Phase 8 ZIP was not present in the active file surface, so this report does not claim byte-for-byte compatibility with that unavailable tree. Phase 9 remains additive and depends only on the public `kernel/types.h`, `ai/device.h`, and `ai/tensor.h` ABI documented in `docs/PHASE9_INTEGRATION.md`.

## Findings corrected

1. **Staging-policy flag was inert.** `AI_DIST_TRANSFER_ALLOW_STAGING` existed but multi-hop routing was permitted even when the flag was absent. Multi-hop routing is now opt-in; direct-only behavior is the conservative default.
2. **Rooted collectives accepted a root outside the participant set.** Broadcast/reduce now require a nonzero root that is one of the participants. Optional roots on other collectives must also belong to the participant set.
3. **Directed reduce routing used the wrong direction.** Tree planning previously tested root-to-peer reachability for reduce. Reduce now evaluates peer-to-root paths; bidirectional tree-style collectives require both directions where appropriate.
4. **Transfer tensor references were not enforced.** A nonzero `remote_tensor_id` is now checked for existence, health, size bounds, and source residency (owner or replica).
5. **Registry validation was too permissive.** Validation now checks enum ranges, duplicate object IDs, duplicate replica/participant references, rooted-collective consistency, and monotonic next-ID invariants.
6. **The original hash step did not actually verify the stored manifest.** The cross-check now runs `sha256sum -c reports/SHA256SUMS`. The generated `CROSSCHECK_REPORT.txt` is deliberately excluded from the stable manifest because it changes on every run.

## Baseline fixture comparison

The packaged `reference_tree/` was compared with the original `aikernel-starter.zip`. Outside the new `distributed.h`/`distributed.c` files, the only substantive fixture changes are adding `src/ai/distributed.c` to the Makefile source list and adding the Phase 9 implementation note to `docs/ROADMAP.md`. No unrelated baseline kernel subsystem was rewritten.

## Verification layers

- GCC C11 compile with `-Wall -Wextra -Werror -pedantic`
- host functional + adversarial tests
- Clang portability compile/test when available
- AddressSanitizer + UndefinedBehaviorSanitizer
- GCC/Clang freestanding compile
- forbidden libc/network/compiler-runtime symbol scan
- module/reference-tree byte-for-byte mirror check
- complete `-nostdlib` kernel ELF link
- stable package hash verification

## Conclusion

The corrected package reaches the Milestone 9 roadmap conclusion: remote accelerators, remote tensors, network transfers, and collectives are represented as graph/resource objects; topology-aware placement, an RDMA-style transfer abstraction, and collective scheduling are implemented at the control-plane/planning layer.

This does not establish real multi-host transport performance, RDMA correctness, NCCL/MPI semantics, security, or fault-tolerant remote execution; those remain outside Phase 9 by design.
