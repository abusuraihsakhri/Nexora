#ifndef NEXORA_BENCH_H
#define NEXORA_BENCH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Platform hooks supplied by the integrated Nexora kernel. */
uint64_t nx_bench_now_ns(void);
void nx_bench_write(const char *data, size_t len);

/* Emit one nexora.bench.v1 JSONL event without heap allocation. */
void nx_bench_emit_u64(
    const char *benchmark,
    const char *metric,
    uint64_t value,
    const char *unit,
    uint64_t iteration,
    const char *variant,
    const char *workload
);

#ifdef __cplusplus
}
#endif

#endif
