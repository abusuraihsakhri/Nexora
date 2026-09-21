# Benchmark Methodology

The harness is designed for architectural comparison, not marketing benchmarks.

Control the following before attributing a difference to Nexora's design: hardware, CPU frequency policy, core affinity, compiler/optimization settings, workload parameters, warm-up, timer calibration, virtualization, memory pressure and background load. QEMU measurements are useful for functional and algorithmic comparisons but should not be presented as bare-metal performance results.

Use at least 10–15 independent process-level repetitions for stable reporting. Keep raw JSONL, `system.json`, source revision and build flags. Inspect medians plus tail metrics; means alone are insufficient for latency workloads.

For memory, Phase 6 reports logical tensor bytes to avoid conflating lifetime policy with glibc allocator retention or RSS accounting. A later bare-metal experiment may additionally report page-frame high-water marks.

For zero-copy IPC, the Linux `shared_mem` variant measures handoff of data already resident in a shared mapping. It does not count producer generation cost. The corresponding Nexora benchmark should time shared-tensor-handle delegation/notification over an already-populated tensor.

For deadline experiments, the included Linux program is a deterministic discrete-event baseline. It tests policy behavior and scheduler computational cost, not Linux kernel real-time scheduling. A future extension may add `SCHED_FIFO`/`SCHED_DEADLINE` experiments where privileges and platform support permit.

## Cross-platform comparability gate

A Linux-vs-Nexora number is admissible only when the benchmark ID and complete `params` object match. Iteration count and warm-up count are therefore deliberately matched between Linux variants in the suite and must be mirrored by kernel telemetry. The analyzer emits `comparability.json` and a report warning when both platforms are present but no parameter group matches.

Performance comparisons also require execution-environment parity. Do not compare native Linux host timings against a Nexora kernel running under software emulation and attribute the difference to OS architecture. Use the same physical machine and comparable execution mode (for example, both under matched virtualization/KVM conditions), document clock calibration, CPU affinity, power state and compiler settings, and retain the raw telemetry.

For the shared-memory IPC test, mapping/handle establishment occurs before the timed loop. The timed quantity is payload handoff after a shared mapping already exists. A Nexora comparison must likewise pre-establish/delegate the tensor handle outside the timed loop. If handle-delegation setup cost is the research question, use a separate benchmark with equivalent Linux descriptor-passing setup (for example, `SCM_RIGHTS`) rather than mixing setup cost into only one side.
