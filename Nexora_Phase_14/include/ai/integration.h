#ifndef AIKERNEL_AI_INTEGRATION_H
#define AIKERNEL_AI_INTEGRATION_H

#include <kernel/types.h>
#include <ai/benchmark.h>

#define AI_PHASE14_STRESS_ROUNDS 48

typedef struct {
    bool passed;
    u32 assertions;
    u32 assertions_passed;
    u32 assertions_failed;
    u32 graphs_validated;
    u32 graphs_completed;
    u32 deadlocks_detected;
    u32 stress_rounds_completed;
    u64 scheduler_dispatches;
    u64 tensor_bytes_accounted;
    u64 heap_before;
    u64 heap_after;
    u64 heap_growth;
    bool capability_isolation_ok;
    bool deadlock_detection_ok;
    bool tensor_accounting_ok;
    bool memory_accounting_ok;
    ai_benchmark_report benchmark;
} ai_phase14_report;

bool ai_phase14_run(ai_phase14_report *report);

#endif
