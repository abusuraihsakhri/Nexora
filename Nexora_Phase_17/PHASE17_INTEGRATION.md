# Integration with the Phase 16 tree

This package is intentionally additive because the Phase 16 archive was not available in the current chat surface.

Recommended merge mapping:

- copy `include/nexora/phase17/*` into the Phase 16 include tree;
- copy `src/phase17/*` into the kernel/runtime source tree;
- add the four Phase 17 source files to the existing build target;
- preserve the test and tooling directories as host-side validation assets;
- wire existing kernel components into the health registry at initialization;
- emit trace events at scheduler dispatch, allocator pressure, accelerator submission/completion, IPC failures, distributed-node transitions, and security denials;
- call watchdog checks from an existing periodic kernel tick or supervision loop rather than creating a new Phase 17 thread.

## Integration contract

Phase 17 assumes only a C11-capable build for its standalone tests. The code itself uses fixed-size storage and does not invoke malloc/free, filesystem I/O, sockets, sleep, or OS threads. The caller supplies monotonic timestamps.

## Suggested component registrations

- scheduler
- memory
- accelerator
- ipc
- storage
- network/distributed fabric
- security/policy
- model runtime

## Failure policy

Phase 17 reports state; it does not unilaterally reboot, kill tasks, or reset devices. Recovery policy remains a higher-level kernel decision. This separation prevents observability code from becoming a hidden control plane.
