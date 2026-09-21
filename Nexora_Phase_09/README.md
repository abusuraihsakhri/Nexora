# Nexora Phase 9 — Distributed AI Resources

This bundle implements the roadmap's distributed-resource milestone as an additive, freestanding kernel module.

## What is included

- remote/local accelerator endpoint objects
- topology/fabric links with bandwidth/latency/health
- remote tensor publication and replica metadata
- RDMA-style transfer request/plan abstraction with opt-in multi-hop staging
- topology-aware shortest-path planning
- topology/load/memory/deadline-aware placement
- collective objects and direction-aware tree/ring communication planning
- endpoint/link degradation and failover
- invariant validation and planning metrics
- host behavioral + adversarial tests and sanitizers
- baseline kernel compile/link proof

## Integrate into the existing Phase 8 tree

Copy:

```text
module/include/ai/distributed.h -> include/ai/distributed.h
module/src/ai/distributed.c     -> src/ai/distributed.c
```

Then add `src/ai/distributed.c` to the kernel's C source list.

Read `docs/PHASE9_INTEGRATION.md` before wiring the module into Phase 8 model/KV-cache/batching/prefetch code.

## Re-run validation

```bash
./scripts/crosscheck.sh
```

The `reference_tree/` directory exists only as a compile/link compatibility fixture based on the original starter ABI. The deep cross-check also verifies that the packaged module and the reference-tree copies are byte-identical. It is not intended to overwrite the evolved Phase 8 tree.

## Current boundary

This phase models distributed resources and plans placement/transfers/collectives. It does not pretend to implement a physical NIC, TCP/IP, RDMA verbs, NCCL, security handshake, or fault-tolerant remote execution protocol.
