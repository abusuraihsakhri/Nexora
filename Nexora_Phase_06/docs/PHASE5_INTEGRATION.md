# Integrating Phase 6 with the Phase-5 Nexora tree

This package is intentionally an overlay: it does not replace the Phase-5 kernel tree.

Copy `bench/`, `scripts/`, `tests/` and `docs/` into the Phase-5 repository, or keep this directory adjacent to it. Kernel-side changes required for a true comparison are instrumentation only:

1. add a calibrated monotonic timestamp primitive (or report cycles explicitly);
2. benchmark `ai_tensor_create` + `ai_tensor_release` with matching sizes;
3. time graph scheduling without executing payload work;
4. expose the tensor allocator/page-frame high-water mark during a fixed lifetime trace;
5. pre-establish/delegate the shared tensor handle outside the timed loop, then time notification/handoff without copying payload bytes (matching the Linux shared-mapping benchmark);
6. run a deterministic deadline workload through the Nexora scheduler;
7. emit one `NEXORA_BENCH` record per variant after timing has completed.

Do not alter the syscall semantics merely to make the benchmark favorable. Keep benchmark instrumentation outside production fast paths where possible.


## Required comparison parity

The current host harness does not make native-host-vs-emulated-kernel performance claims. If Nexora is run in QEMU, use a Linux reference environment with comparable virtualization/acceleration and CPU pinning, or treat the result as functional/algorithmic only. Record whether QEMU uses TCG, KVM, or another accelerator and record the timing source/calibration used by Nexora.
