#include "common.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static void usage(const char *prog) {
    fprintf(stderr, "usage: %s [--mode malloc|mmap] [--iterations N] [--warmup N] [--size BYTES]\n", prog);
}

int main(int argc, char **argv) {
    const char *mode = bench_arg_value(argc, argv, "--mode", "malloc");
    const uint64_t iterations = bench_parse_u64(bench_arg_value(argc, argv, "--iterations", "20000"), "iterations");
    const uint64_t warmup = bench_parse_u64(bench_arg_value(argc, argv, "--warmup", "2000"), "warmup");
    const uint64_t size = bench_parse_u64(bench_arg_value(argc, argv, "--size", "4096"), "size");
    if (iterations == 0 || size == 0 || (strcmp(mode, "malloc") != 0 && strcmp(mode, "mmap") != 0)) {
        usage(argv[0]);
        return 2;
    }

    uint64_t *samples = calloc((size_t)iterations, sizeof(*samples));
    if (!samples) {
        perror("calloc");
        return 2;
    }

    volatile unsigned char sink = 0;
    for (uint64_t i = 0; i < warmup + iterations; ++i) {
        uint64_t t0 = bench_now_ns();
        void *ptr = NULL;
        if (strcmp(mode, "malloc") == 0) {
            ptr = malloc((size_t)size);
            if (!ptr) {
                perror("malloc");
                free(samples);
                return 2;
            }
            ((unsigned char *)ptr)[0] = (unsigned char)i;
            ((unsigned char *)ptr)[size - 1] ^= (unsigned char)(i >> 8);
            sink ^= ((unsigned char *)ptr)[0];
            free(ptr);
        } else {
            ptr = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (ptr == MAP_FAILED) {
                perror("mmap");
                free(samples);
                return 2;
            }
            ((unsigned char *)ptr)[0] = (unsigned char)i;
            ((unsigned char *)ptr)[size - 1] ^= (unsigned char)(i >> 8);
            sink ^= ((unsigned char *)ptr)[0];
            if (munmap(ptr, (size_t)size) != 0) {
                perror("munmap");
                free(samples);
                return 2;
            }
        }
        uint64_t t1 = bench_now_ns();
        if (i >= warmup) {
            samples[i - warmup] = t1 - t0;
        }
    }

    const double mean = bench_mean(samples, (size_t)iterations);
    uint64_t p50 = bench_percentile_u64(samples, (size_t)iterations, 0.50);
    uint64_t p95 = bench_percentile_u64(samples, (size_t)iterations, 0.95);
    uint64_t p99 = bench_percentile_u64(samples, (size_t)iterations, 0.99);

    bench_print_metrics_header("allocation_latency", mode);
    printf("\"params\":{\"iterations\":%" PRIu64 ",\"warmup\":%" PRIu64 ",\"size_bytes\":%" PRIu64 "},", iterations, warmup, size);
    printf("\"metrics\":{\"mean_ns\":%.3f,\"median_ns\":%" PRIu64 ",\"p95_ns\":%" PRIu64 ",\"p99_ns\":%" PRIu64 "},", mean, p50, p95, p99);
    printf("\"checksum\":%u}\n", (unsigned)sink);
    free(samples);
    return 0;
}
