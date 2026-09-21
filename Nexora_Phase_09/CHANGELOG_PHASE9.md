# Phase 9 changelog

## Added

- distributed endpoint/resource registry
- remote tensor publication and replica metadata
- transport-neutral fabric link model
- bounded shortest-path planner
- RDMA preference and direct-transfer constraint
- topology/load/memory/deadline-aware endpoint placement
- collective resource objects and tree/ring planning
- endpoint/link health states and failover behavior
- internal metrics and invariant validation
- host test suite and automated cross-check

## Not added

- real NIC/RDMA driver
- TCP/IP stack
- NCCL/MPI binding
- remote execution protocol
- cryptographic peer identity
- distributed consensus

## Deep cross-check corrections

- made multi-hop staging explicitly opt-in via `AI_DIST_TRANSFER_ALLOW_STAGING`
- rejected rooted collectives whose root is not a participant
- corrected directed reduce tree-path orientation
- validated nonzero remote-tensor references during transfer planning
- strengthened registry invariant and enum validation
- added adversarial tests, Clang portability, ASan/UBSan, mirror checks, and real SHA-256 manifest verification
- excluded the generated cross-check report from the stable hash manifest
