#include "common.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t priority;
    uint8_t done;
    uint8_t ready;
} node_t;

typedef struct {
    uint32_t *data;
    size_t len;
    size_t cap;
    node_t *nodes;
} maxheap_t;

static int higher(const maxheap_t *h, uint32_t a, uint32_t b) {
    const uint32_t pa = h->nodes[a].priority;
    const uint32_t pb = h->nodes[b].priority;
    return pa > pb || (pa == pb && a < b);
}

static void heap_push(maxheap_t *h, uint32_t v) {
    size_t i = h->len++;
    h->data[i] = v;
    while (i > 0) {
        size_t p = (i - 1) / 2;
        if (higher(h, h->data[p], h->data[i])) break;
        uint32_t tmp = h->data[p]; h->data[p] = h->data[i]; h->data[i] = tmp;
        i = p;
    }
}

static uint32_t heap_pop(maxheap_t *h) {
    uint32_t out = h->data[0];
    h->data[0] = h->data[--h->len];
    size_t i = 0;
    for (;;) {
        size_t l = i * 2 + 1, r = l + 1, best = i;
        if (l < h->len && higher(h, h->data[l], h->data[best])) best = l;
        if (r < h->len && higher(h, h->data[r], h->data[best])) best = r;
        if (best == i) break;
        uint32_t tmp = h->data[i]; h->data[i] = h->data[best]; h->data[best] = tmp;
        i = best;
    }
    return out;
}

static void reset_nodes(node_t *nodes, uint64_t n, uint64_t seed) {
    uint64_t s = seed;
    for (uint64_t i = 0; i < n; ++i) {
        nodes[i].priority = (uint32_t)(bench_xorshift64(&s) & 0xffffu);
        nodes[i].done = 0;
        nodes[i].ready = (i == 0);
    }
}

static inline void release_children(node_t *nodes, uint64_t n, uint32_t idx) {
    uint64_t c1 = (uint64_t)idx * 2 + 1;
    uint64_t c2 = c1 + 1;
    if (c1 < n) nodes[c1].ready = 1;
    if (c2 < n) nodes[c2].ready = 1;
}

static uint64_t run_scan(node_t *nodes, uint64_t n, uint64_t *checksum) {
    uint64_t t0 = bench_now_ns();
    for (uint64_t completed = 0; completed < n; ++completed) {
        uint32_t best = UINT32_MAX;
        for (uint64_t i = 0; i < n; ++i) {
            if (!nodes[i].ready || nodes[i].done) continue;
            if (best == UINT32_MAX || nodes[i].priority > nodes[best].priority ||
                (nodes[i].priority == nodes[best].priority && i < best)) {
                best = (uint32_t)i;
            }
        }
        if (best == UINT32_MAX) {
            fprintf(stderr, "scheduler deadlock\n");
            exit(2);
        }
        nodes[best].done = 1;
        *checksum ^= ((uint64_t)best << 32) | nodes[best].priority;
        release_children(nodes, n, best);
    }
    return bench_now_ns() - t0;
}

static uint64_t run_ready_queue(node_t *nodes, uint64_t n, uint64_t *checksum) {
    uint32_t *storage = calloc((size_t)n, sizeof(*storage));
    if (!storage) { perror("calloc"); exit(2); }
    maxheap_t heap = {.data = storage, .len = 0, .cap = (size_t)n, .nodes = nodes};
    (void)heap.cap;
    heap_push(&heap, 0);
    uint64_t t0 = bench_now_ns();
    for (uint64_t completed = 0; completed < n; ++completed) {
        if (heap.len == 0) { fprintf(stderr, "scheduler deadlock\n"); exit(2); }
        uint32_t best = heap_pop(&heap);
        nodes[best].done = 1;
        *checksum ^= ((uint64_t)best << 32) | nodes[best].priority;
        uint64_t c1 = (uint64_t)best * 2 + 1;
        uint64_t c2 = c1 + 1;
        if (c1 < n) { nodes[c1].ready = 1; heap_push(&heap, (uint32_t)c1); }
        if (c2 < n) { nodes[c2].ready = 1; heap_push(&heap, (uint32_t)c2); }
    }
    uint64_t elapsed = bench_now_ns() - t0;
    free(storage);
    return elapsed;
}

int main(int argc, char **argv) {
    const char *variant = bench_arg_value(argc, argv, "--variant", "ready_queue");
    const uint64_t nodes_n = bench_parse_u64(bench_arg_value(argc, argv, "--nodes", "4095"), "nodes");
    const uint64_t rounds = bench_parse_u64(bench_arg_value(argc, argv, "--rounds", "50"), "rounds");
    const uint64_t seed = bench_parse_u64(bench_arg_value(argc, argv, "--seed", "424242"), "seed");
    if (nodes_n == 0 || nodes_n > UINT32_MAX || rounds == 0 ||
        (strcmp(variant, "scan") != 0 && strcmp(variant, "ready_queue") != 0)) {
        fprintf(stderr, "invalid arguments\n");
        return 2;
    }

    node_t *nodes = calloc((size_t)nodes_n, sizeof(*nodes));
    uint64_t *samples = calloc((size_t)rounds, sizeof(*samples));
    if (!nodes || !samples) { perror("calloc"); free(nodes); free(samples); return 2; }
    uint64_t checksum = 0;

    for (uint64_t r = 0; r < rounds + 2; ++r) {
        reset_nodes(nodes, nodes_n, seed + r);
        uint64_t elapsed = strcmp(variant, "scan") == 0
            ? run_scan(nodes, nodes_n, &checksum)
            : run_ready_queue(nodes, nodes_n, &checksum);
        if (r >= 2) samples[r - 2] = elapsed / nodes_n;
    }

    double mean = bench_mean(samples, (size_t)rounds);
    uint64_t p50 = bench_percentile_u64(samples, (size_t)rounds, 0.50);
    uint64_t p95 = bench_percentile_u64(samples, (size_t)rounds, 0.95);
    uint64_t p99 = bench_percentile_u64(samples, (size_t)rounds, 0.99);

    bench_print_metrics_header("graph_scheduling_overhead", variant);
    printf("\"params\":{\"nodes\":%" PRIu64 ",\"rounds\":%" PRIu64 ",\"seed\":%" PRIu64 ",\"dag\":\"binary_tree\"},", nodes_n, rounds, seed);
    printf("\"metrics\":{\"mean_ns_per_node\":%.3f,\"median_ns_per_node\":%" PRIu64 ",\"p95_ns_per_node\":%" PRIu64 ",\"p99_ns_per_node\":%" PRIu64 "},", mean, p50, p95, p99);
    printf("\"checksum\":%" PRIu64 "}\n", checksum);
    free(nodes);
    free(samples);
    return 0;
}
