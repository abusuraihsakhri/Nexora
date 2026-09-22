# Known implementation limits

This document is the authoritative boundary for claims about the current Nexora-RK implementation.

| Area | Current state | Consequence |
| --- | --- | --- |
| CPU topology | One CPU | No SMP performance or safety claims |
| User VM | Structural ELF validation only | Boot path does not launch a production Ring-3 process |
| Tensor mapping | Disabled (`ENOSYS`) | No raw kernel-address exposure; real VM mapping still required |
| Accelerators | Semantic/simulated device classes | No real GPU/NPU execution claim |
| Scheduler | Deterministic synchronous executor | No preemption or asynchronous completion yet |
| Time | No calibrated monotonic kernel source | Execution result timestamps remain zero |
| Slab allocator | Object reuse within allocated pages | Fully free pages are not returned to frame allocator |
| Page faults | Fixups + terminal handling | No scheduler-driven user-process recovery yet |
| Networking/storage | Not active kernel mechanisms | Distributed/resource concepts remain research interfaces |
| Security model | Handle rights + delegation prototype | Not a formally verified capability system |

A limitation should be removed from this table only when the mechanism is implemented **and** covered by executable validation.
