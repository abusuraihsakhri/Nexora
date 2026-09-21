# Kernel adapter

`nexora_bench.c` is allocation-free and suitable for kernel use after replacing the standard integer/type includes if the Phase 14 freestanding tree does not expose `<stdint.h>` / `<stddef.h>`.

Two hooks must be bound:

- `nx_bench_now_ns()` → Phase 14 monotonic clock
- `nx_bench_write()` → Phase 14 serial/log sink

Do not emit untrusted strings through the encoder. Benchmark identifiers should be compile-time constants.
