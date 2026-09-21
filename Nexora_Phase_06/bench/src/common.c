#include "common.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint64_t bench_now_ns(void) {
    struct timespec ts;
#ifdef CLOCK_MONOTONIC_RAW
    const clockid_t clock_id = CLOCK_MONOTONIC_RAW;
#else
    const clockid_t clock_id = CLOCK_MONOTONIC;
#endif
    if (clock_gettime(clock_id, &ts) != 0) {
        perror("clock_gettime");
        exit(2);
    }
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}

uint64_t bench_parse_u64(const char *s, const char *name) {
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(s, &end, 10);
    if (errno != 0 || s == end || *end != '\0') {
        fprintf(stderr, "invalid %s: %s\n", name, s);
        exit(2);
    }
    return (uint64_t)value;
}

const char *bench_arg_value(int argc, char **argv, const char *name, const char *fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], name) == 0) {
            return argv[i + 1];
        }
    }
    return fallback;
}

int bench_has_flag(int argc, char **argv, const char *name) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

uint64_t bench_xorshift64(uint64_t *state) {
    uint64_t x = *state;
    if (x == 0) {
        x = UINT64_C(0x9e3779b97f4a7c15);
    }
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

double bench_mean(const uint64_t *values, size_t n) {
    if (n == 0) {
        return 0.0;
    }
    long double sum = 0.0L;
    for (size_t i = 0; i < n; ++i) {
        sum += (long double)values[i];
    }
    return (double)(sum / (long double)n);
}

static int cmp_u64(const void *a, const void *b) {
    const uint64_t aa = *(const uint64_t *)a;
    const uint64_t bb = *(const uint64_t *)b;
    return (aa > bb) - (aa < bb);
}

uint64_t bench_percentile_u64(uint64_t *values, size_t n, double q) {
    if (n == 0) {
        return 0;
    }
    if (q < 0.0) q = 0.0;
    if (q > 1.0) q = 1.0;
    qsort(values, n, sizeof(*values), cmp_u64);
    size_t idx = (size_t)(q * (double)(n - 1));
    return values[idx];
}

void bench_print_metrics_header(const char *benchmark, const char *variant) {
    printf("{\"schema\":\"nexora.bench.v1\",\"benchmark\":\"%s\",\"platform\":\"linux\",\"variant\":\"%s\",", benchmark, variant);
}
