# Nexora Phase 9 — Distributed AI Resources

## Objective

Treat distributed AI infrastructure as kernel-visible resource metadata rather than as an opaque userspace cluster abstraction.

Phase 9 introduces first-class descriptions of:

- remote compute endpoints (CPU/GPU/NPU)
- remote/published tensors and replicas
- network/fabric links
- topology-aware transfer paths
- placement across local and remote accelerators
- collective operations
- endpoint/link health and failover
- measurable planning metrics

The module is intentionally a **control-plane abstraction**, not a network stack or vendor RDMA driver. It produces resource and transfer plans that a later concrete transport can execute.

## Ten implementation steps

1. **Distributed resource registry** — fixed-capacity, freestanding metadata store for endpoints, links, remote tensors, and collectives.
2. **Remote accelerator representation** — local/remote CPU/GPU/NPU endpoints with memory, queue delay, abstract compute rate, NUMA domain, and health.
3. **Topology graph** — directed/bidirectional fabric links with bandwidth, latency, policy penalty, transport type, and health.
4. **Remote tensor objects** — owner, origin tensor ID, generation, size, flags, and replica locations.
5. **RDMA-style transfer abstraction** — transfer request/plan objects independent of the eventual concrete transport driver.
6. **Topology-aware path planning** — weighted shortest-path selection based on transfer time, transport preference, degradation, and policy penalty.
7. **Topology/load-aware placement** — endpoint choice using transfer cost, queue delay, compute estimate, memory pressure, locality preference, and deadline penalty.
8. **Collective operations** — broadcast, reduce, all-reduce, all-gather, reduce-scatter, and barrier with tree/ring planning.
9. **Failure/health model** — endpoint and link health states allow degraded-cost planning and hard failover around unavailable resources.
10. **Validation/metrics/tests** — invariant validation, planning counters, deadline-miss prediction, host tests, kernel build check, and integration documentation.

## Deliberate boundaries

Phase 9 does **not** claim to provide:

- TCP/IP
- InfiniBand verbs
- RoCE
- real NIC DMA programming
- distributed consensus
- remote memory coherence
- cryptographic authentication
- multi-host clock synchronization
- fault-tolerant job replay

Those require dedicated transport, security, and reliability work. Phase 9 defines the kernel-facing object model and planner first.

## Core cost model

For a candidate endpoint:

```text
score =
    topology_transfer_time
  + endpoint_queue_delay
  + execution_estimate
  + memory_pressure_penalty
  + locality_penalty
  + deadline_miss_penalty
```

For a fabric link:

```text
transfer_time =
    base_latency
  + policy_penalty
  + bytes / bandwidth
  + degradation/preference penalties
```

All arithmetic is integer nanoseconds and is suitable for a freestanding kernel build.

## Safety and correctness properties

- A DOWN endpoint cannot be selected.
- A DOWN link cannot appear in a path.
- Placement rejects insufficient device memory.
- Multi-hop staging is opt-in; direct-only transfer requests cannot silently use a staged route.
- Remote tensor replicas must refer to registered endpoints.
- A transfer that names a remote tensor must originate from its owner/replica and cannot exceed the tensor size.
- Collectives reject duplicate/unknown participants and invalid roots at creation.
- Directed tree collectives validate communication in the operation-specific direction (for example, peer-to-root for reduce).
- Registry validation catches broken object references and capacity violations.
- Path and scoring arithmetic uses saturation to avoid wraparound into artificially low costs.

## Exit criteria

Phase 9 is complete when:

- the distributed module compiles freestanding with the baseline kernel ABI;
- host tests cover topology routing, direct-transfer constraints, remote tensors, placement, memory admission, collectives, link failure, deadline prediction, and metrics;
- the kernel ELF links with the module enabled;
- no libc allocation/network dependency is introduced into the kernel module;
- the integration surface is additive and does not require changing Phase 8 APIs.
