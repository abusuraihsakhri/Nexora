#include "nexora_bench.h"

/*
 * Minimal JSONL encoder for benchmark telemetry.
 * Assumption: benchmark/metric/unit/variant/workload are trusted static tokens
 * without quotes/control characters. Use fixed identifiers, not user input.
 */

static size_t append_str(char *dst, size_t cap, size_t pos, const char *s) {
    if (!s) s = "";
    while (*s && pos + 1 < cap) dst[pos++] = *s++;
    return pos;
}

static size_t append_u64(char *dst, size_t cap, size_t pos, uint64_t v) {
    char tmp[32];
    size_t n = 0;
    if (v == 0) {
        if (pos + 1 < cap) dst[pos++] = '0';
        return pos;
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n && pos + 1 < cap) dst[pos++] = tmp[--n];
    return pos;
}

void nx_bench_emit_u64(
    const char *benchmark,
    const char *metric,
    uint64_t value,
    const char *unit,
    uint64_t iteration,
    const char *variant,
    const char *workload
) {
    char b[512];
    size_t p = 0;
#define S(x) do { p = append_str(b, sizeof(b), p, (x)); } while (0)
#define U(x) do { p = append_u64(b, sizeof(b), p, (x)); } while (0)
    S("{\"schema\":\"nexora.bench.v1\",\"run_id\":\"kernel\",\"benchmark\":\""); S(benchmark);
    S("\",\"metric\":\""); S(metric);
    S("\",\"value\":"); U(value);
    S(",\"unit\":\""); S(unit);
    S("\",\"iteration\":"); U(iteration);
    S(",\"variant\":\""); S(variant);
    S("\",\"workload\":\""); S(workload);
    S("\",\"timestamp_ns\":"); U(nx_bench_now_ns());
    S("}\n");
#undef S
#undef U
    nx_bench_write(b, p);
}
