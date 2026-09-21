#include "common.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t birth;
    uint64_t death;
    uint64_t bytes;
} tensor_t;

int main(int argc, char **argv) {
    const char *variant = bench_arg_value(argc, argv, "--variant", "lifetime_aware");
    const uint64_t n = bench_parse_u64(bench_arg_value(argc, argv, "--tensors", "20000"), "tensors");
    const uint64_t max_lifetime = bench_parse_u64(bench_arg_value(argc, argv, "--max-lifetime", "128"), "max-lifetime");
    const uint64_t min_bytes = bench_parse_u64(bench_arg_value(argc, argv, "--min-bytes", "4096"), "min-bytes");
    const uint64_t max_bytes = bench_parse_u64(bench_arg_value(argc, argv, "--max-bytes", "1048576"), "max-bytes");
    const uint64_t seed = bench_parse_u64(bench_arg_value(argc, argv, "--seed", "20260920"), "seed");
    if (n == 0 || max_lifetime == 0 || min_bytes == 0 || max_bytes < min_bytes ||
        (strcmp(variant, "retain_all") != 0 && strcmp(variant, "lifetime_aware") != 0)) {
        fprintf(stderr, "invalid arguments\n");
        return 2;
    }

    tensor_t *tensors = calloc((size_t)n, sizeof(*tensors));
    uint64_t *release_bytes = calloc((size_t)(n + max_lifetime + 1), sizeof(*release_bytes));
    if (!tensors || !release_bytes) { perror("calloc"); free(tensors); free(release_bytes); return 2; }

    uint64_t state = seed;
    uint64_t total_bytes = 0;
    for (uint64_t i = 0; i < n; ++i) {
        uint64_t life = 1 + bench_xorshift64(&state) % max_lifetime;
        uint64_t span = max_bytes - min_bytes + 1;
        uint64_t bytes = min_bytes + bench_xorshift64(&state) % span;
        tensors[i].birth = i;
        tensors[i].death = i + life;
        tensors[i].bytes = bytes;
        total_bytes += bytes;
    }

    uint64_t current = 0, peak = 0, releases = 0;
    uint64_t t0 = bench_now_ns();
    for (uint64_t step = 0; step < n; ++step) {
        if (strcmp(variant, "lifetime_aware") == 0) {
            current -= release_bytes[step];
            if (release_bytes[step]) ++releases;
        }
        current += tensors[step].bytes;
        if (strcmp(variant, "lifetime_aware") == 0) {
            release_bytes[tensors[step].death] += tensors[step].bytes;
        }
        if (current > peak) peak = current;
    }
    if (strcmp(variant, "retain_all") == 0) {
        peak = total_bytes;
        current = total_bytes;
    }
    uint64_t elapsed = bench_now_ns() - t0;

    bench_print_metrics_header("tensor_lifetime_memory_peak", variant);
    printf("\"params\":{\"tensors\":%" PRIu64 ",\"max_lifetime_steps\":%" PRIu64 ",\"min_bytes\":%" PRIu64 ",\"max_bytes\":%" PRIu64 ",\"seed\":%" PRIu64 "},", n, max_lifetime, min_bytes, max_bytes, seed);
    printf("\"metrics\":{\"logical_peak_bytes\":%" PRIu64 ",\"total_tensor_bytes\":%" PRIu64 ",\"simulation_ns\":%" PRIu64 ",\"release_events\":%" PRIu64 "},", peak, total_bytes, elapsed, releases);
    printf("\"checksum\":%" PRIu64 "}\n", current ^ peak ^ total_bytes);

    free(tensors);
    free(release_bytes);
    return 0;
}
