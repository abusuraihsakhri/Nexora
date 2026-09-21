#include "common.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint64_t arrival;
    uint64_t service;
    uint64_t deadline;
    uint8_t done;
} job_t;

static int choose_fifo(job_t *jobs, uint64_t n, uint64_t now) {
    for (uint64_t i = 0; i < n; ++i) {
        if (!jobs[i].done && jobs[i].arrival <= now) return (int)i;
    }
    return -1;
}

static int choose_edf(job_t *jobs, uint64_t n, uint64_t now) {
    int best = -1;
    for (uint64_t i = 0; i < n; ++i) {
        if (jobs[i].done || jobs[i].arrival > now) continue;
        if (best < 0 || jobs[i].deadline < jobs[(uint64_t)best].deadline ||
            (jobs[i].deadline == jobs[(uint64_t)best].deadline && i < (uint64_t)best)) {
            best = (int)i;
        }
    }
    return best;
}

static uint64_t next_arrival(job_t *jobs, uint64_t n) {
    uint64_t best = UINT64_MAX;
    for (uint64_t i = 0; i < n; ++i) {
        if (!jobs[i].done && jobs[i].arrival < best) best = jobs[i].arrival;
    }
    return best;
}

int main(int argc, char **argv) {
    const char *policy = bench_arg_value(argc, argv, "--policy", "edf");
    const uint64_t n = bench_parse_u64(bench_arg_value(argc, argv, "--jobs", "5000"), "jobs");
    const uint64_t arrival_gap = bench_parse_u64(bench_arg_value(argc, argv, "--arrival-gap-ns", "100000"), "arrival-gap-ns");
    const uint64_t service_min = bench_parse_u64(bench_arg_value(argc, argv, "--service-min-ns", "20000"), "service-min-ns");
    const uint64_t service_max = bench_parse_u64(bench_arg_value(argc, argv, "--service-max-ns", "180000"), "service-max-ns");
    const uint64_t slack_min = bench_parse_u64(bench_arg_value(argc, argv, "--slack-min-ns", "250000"), "slack-min-ns");
    const uint64_t slack_max = bench_parse_u64(bench_arg_value(argc, argv, "--slack-max-ns", "1000000"), "slack-max-ns");
    const uint64_t seed = bench_parse_u64(bench_arg_value(argc, argv, "--seed", "6006"), "seed");
    if (n == 0 || service_max < service_min || slack_max < slack_min ||
        (strcmp(policy, "fifo") != 0 && strcmp(policy, "edf") != 0)) {
        fprintf(stderr, "invalid arguments\n");
        return 2;
    }

    job_t *jobs = calloc((size_t)n, sizeof(*jobs));
    uint64_t *latencies = calloc((size_t)n, sizeof(*latencies));
    if (!jobs || !latencies) { perror("calloc"); free(jobs); free(latencies); return 2; }
    uint64_t state = seed;
    for (uint64_t i = 0; i < n; ++i) {
        uint64_t service_span = service_max - service_min + 1;
        uint64_t slack_span = slack_max - slack_min + 1;
        jobs[i].arrival = i * arrival_gap;
        jobs[i].service = service_min + bench_xorshift64(&state) % service_span;
        jobs[i].deadline = jobs[i].arrival + slack_min + bench_xorshift64(&state) % slack_span;
    }

    uint64_t now = 0, misses = 0;
    uint64_t sched_start = bench_now_ns();
    for (uint64_t completed = 0; completed < n; ++completed) {
        int idx = strcmp(policy, "fifo") == 0 ? choose_fifo(jobs, n, now) : choose_edf(jobs, n, now);
        if (idx < 0) {
            uint64_t next = next_arrival(jobs, n);
            if (next == UINT64_MAX) { fprintf(stderr, "simulation deadlock\n"); return 2; }
            now = next;
            idx = strcmp(policy, "fifo") == 0 ? choose_fifo(jobs, n, now) : choose_edf(jobs, n, now);
        }
        job_t *j = &jobs[(uint64_t)idx];
        if (now < j->arrival) now = j->arrival;
        now += j->service;
        latencies[completed] = now - j->arrival;
        if (now > j->deadline) ++misses;
        j->done = 1;
    }
    uint64_t scheduler_cpu_ns = bench_now_ns() - sched_start;

    double mean = bench_mean(latencies, (size_t)n);
    uint64_t p50 = bench_percentile_u64(latencies, (size_t)n, 0.50);
    uint64_t p95 = bench_percentile_u64(latencies, (size_t)n, 0.95);
    uint64_t p99 = bench_percentile_u64(latencies, (size_t)n, 0.99);
    double miss_rate = (double)misses / (double)n;

    bench_print_metrics_header("deadline_tail_latency", policy);
    printf("\"params\":{\"jobs\":%" PRIu64 ",\"arrival_gap_ns\":%" PRIu64 ",\"service_min_ns\":%" PRIu64 ",\"service_max_ns\":%" PRIu64 ",\"slack_min_ns\":%" PRIu64 ",\"slack_max_ns\":%" PRIu64 ",\"seed\":%" PRIu64 "},", n, arrival_gap, service_min, service_max, slack_min, slack_max, seed);
    printf("\"metrics\":{\"mean_latency_ns\":%.3f,\"median_latency_ns\":%" PRIu64 ",\"p95_latency_ns\":%" PRIu64 ",\"p99_latency_ns\":%" PRIu64 ",\"deadline_miss_rate\":%.9f,\"scheduler_cpu_ns\":%" PRIu64 "},", mean, p50, p95, p99, miss_rate, scheduler_cpu_ns);
    printf("\"checksum\":%" PRIu64 "}\n", now ^ misses);

    free(jobs);
    free(latencies);
    return 0;
}
