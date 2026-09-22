#ifndef AIKERNEL_AI_BENCHMARK_H
#define AIKERNEL_AI_BENCHMARK_H

#include <kernel/types.h>

typedef struct {
    bool passed;
    u32 scenarios_run;
    u32 scenarios_completed;
    u32 nodes_total;
    u32 edges_total;
    u64 dispatches;
    u64 picks;
    u64 candidates_scanned;
    u64 selection_efficiency_ppm;
} ai_benchmark_report;

bool ai_benchmark_run(ai_benchmark_report *report);

#endif
