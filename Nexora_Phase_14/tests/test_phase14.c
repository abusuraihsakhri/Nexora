#include <stdio.h>
#include <ai/benchmark.h>
#include <ai/capability.h>
#include <ai/integration.h>
#include <ai/scheduler.h>
#include <ai/tensor.h>
#include <ai/work.h>
#include <kernel/memory.h>

static unsigned tests_run = 0;
static unsigned tests_failed = 0;

#define CHECK(condition) do { \
    tests_run++; \
    if (!(condition)) { \
        tests_failed++; \
        printf("not ok %u - %s:%d: %s\n", tests_run, __FILE__, __LINE__, #condition); \
    } else { \
        printf("ok %u - %s\n", tests_run, #condition); \
    } \
} while (0)

static void reset_runtime(void) {
    early_heap_init();
    ai_tensor_system_init();
}

static void test_tensor_accounting(void) {
    reset_runtime();
    u64 shape[2] = {4, 8};
    ai_tensor *a = ai_tensor_create(
        "a", AI_DTYPE_F16, 2, shape, AI_LOC_CPU_RAM, AI_TENSOR_EPHEMERAL
    );
    ai_tensor *b = ai_tensor_create(
        "b", AI_DTYPE_F32, 2, shape, AI_LOC_GPU_HBM, AI_TENSOR_PERSISTENT
    );

    CHECK(ai_tensor_validate(a));
    CHECK(ai_tensor_validate(b));
    CHECK(a->bytes == 64);
    CHECK(b->bytes == 128);
    CHECK(ai_tensor_total_bytes() == 192);
    CHECK(ai_tensor_bytes_at_location(AI_LOC_CPU_RAM) == 64);
    CHECK(ai_tensor_bytes_at_location(AI_LOC_GPU_HBM) == 128);
}

static void test_graph_validation(void) {
    reset_runtime();
    ai_work_graph graph;
    ai_work_graph_init(&graph);
    ai_work_node *a = ai_work_add(&graph, "a", AI_OP_NOOP, 1, 100, AI_DEVICE_CPU);
    ai_work_node *b = ai_work_add(&graph, "b", AI_OP_NOOP, 1, 200, AI_DEVICE_CPU);
    ai_work_add_dependency(b, a->id);

    ai_graph_validation validation;
    CHECK(ai_work_graph_validate(&graph, &validation));
    CHECK(validation.valid);
    CHECK(validation.node_count == 2);
    CHECK(validation.edge_count == 1);

    ai_work_graph missing;
    ai_work_graph_init(&missing);
    ai_work_node *m = ai_work_add(&missing, "missing", AI_OP_NOOP, 1, 1, AI_DEVICE_CPU);
    ai_work_add_dependency(m, 999);
    CHECK(!ai_work_graph_validate(&missing, &validation));
    CHECK(validation.missing_dependencies == 1);

    ai_work_graph cycle;
    ai_work_graph_init(&cycle);
    ai_work_node *c1 = ai_work_add(&cycle, "c1", AI_OP_NOOP, 1, 1, AI_DEVICE_CPU);
    ai_work_node *c2 = ai_work_add(&cycle, "c2", AI_OP_NOOP, 1, 1, AI_DEVICE_CPU);
    ai_work_add_dependency(c1, c2->id);
    ai_work_add_dependency(c2, c1->id);
    CHECK(!ai_work_graph_validate(&cycle, &validation));
    CHECK(validation.cycle_nodes == 2);
}

static void test_scheduler(void) {
    reset_runtime();
    ai_work_graph graph;
    ai_work_graph_init(&graph);
    ai_work_node *late = ai_work_add(&graph, "late", AI_OP_NOOP, 50, 5000, AI_DEVICE_CPU);
    ai_work_node *early = ai_work_add(&graph, "early", AI_OP_NOOP, 50, 1000, AI_DEVICE_CPU);
    ai_work_node *high = ai_work_add(&graph, "high", AI_OP_NOOP, 80, 9000, AI_DEVICE_CPU);

    ai_scheduler scheduler;
    ai_scheduler_init(&scheduler, &graph);
    CHECK(ai_scheduler_pick(&scheduler) == high);
    ai_scheduler_mark_running(&scheduler, high);
    ai_scheduler_mark_done(&scheduler, high);
    CHECK(ai_scheduler_pick(&scheduler) == early);
    ai_scheduler_mark_running(&scheduler, early);
    ai_scheduler_mark_done(&scheduler, early);
    CHECK(ai_scheduler_pick(&scheduler) == late);

    ai_scheduler_mark_running(&scheduler, late);
    ai_scheduler_mark_done(&scheduler, late);
    ai_scheduler_run_report report;
    CHECK(ai_scheduler_run_to_completion(&scheduler, &report));
    CHECK(report.completed_nodes == 3);
    CHECK(scheduler.dispatch_count == 3);
    CHECK(scheduler.candidate_scan_count >= 9);
}

static void test_deadlock_detection(void) {
    reset_runtime();
    ai_work_graph graph;
    ai_work_graph_init(&graph);
    ai_work_node *a = ai_work_add(&graph, "a", AI_OP_NOOP, 1, 1, AI_DEVICE_CPU);
    ai_work_node *b = ai_work_add(&graph, "b", AI_OP_NOOP, 1, 1, AI_DEVICE_CPU);
    ai_work_add_dependency(a, b->id);
    ai_work_add_dependency(b, a->id);

    ai_scheduler scheduler;
    ai_scheduler_run_report report;
    ai_scheduler_init(&scheduler, &graph);
    CHECK(!ai_scheduler_run_to_completion(&scheduler, &report));
    CHECK(report.deadlocked);
    CHECK(scheduler.deadlock_count == 1);
}

static void test_capabilities(void) {
    ai_capability cap = ai_cap_create(
        7, AI_CAP_TENSOR_READ | AI_CAP_MODEL_USE | AI_CAP_GPU_USE
    );
    CHECK(ai_cap_has(&cap, AI_CAP_TENSOR_READ));
    CHECK(ai_cap_has(&cap, AI_CAP_MODEL_USE));
    CHECK(ai_cap_has(&cap, AI_CAP_GPU_USE));
    CHECK(!ai_cap_has(&cap, AI_CAP_TENSOR_WRITE));
    CHECK(!ai_cap_has(&cap, AI_CAP_NETWORK));
    CHECK(!ai_cap_has(&cap, AI_CAP_ADMIN));
}

static void test_memory_metrics(void) {
    reset_runtime();
    CHECK(early_heap_can_alloc(32, 16));
    (void)kalloc(7, 1);
    (void)kalloc(16, 16);

    early_heap_stats stats;
    early_heap_get_stats(&stats);
    CHECK(stats.allocations == 2);
    CHECK(stats.requested_bytes == 23);
    CHECK(stats.padding_bytes == 9);
    CHECK(stats.used == 32);
    CHECK(stats.high_watermark == 32);
    CHECK(!early_heap_can_alloc(8, 3));
}

static void test_benchmark(void) {
    reset_runtime();
    ai_benchmark_report report;
    CHECK(ai_benchmark_run(&report));
    CHECK(report.scenarios_run == 4);
    CHECK(report.scenarios_completed == 4);
    CHECK(report.nodes_total == 60);
    CHECK(report.edges_total == 56);
    CHECK(report.dispatches == 60);
    CHECK(report.candidates_scanned > report.dispatches);
    CHECK(report.selection_efficiency_ppm > 0);
}

static void test_phase14_suite(void) {
    reset_runtime();
    ai_phase14_report report;
    CHECK(ai_phase14_run(&report));
    CHECK(report.passed);
    CHECK(report.assertions_failed == 0);
    CHECK(report.stress_rounds_completed == AI_PHASE14_STRESS_ROUNDS);
    CHECK(report.deadlocks_detected == 1);
    CHECK(report.graphs_validated == AI_PHASE14_STRESS_ROUNDS + 1);
    CHECK(report.graphs_completed == AI_PHASE14_STRESS_ROUNDS + 1);
    CHECK(report.capability_isolation_ok);
    CHECK(report.deadlock_detection_ok);
    CHECK(report.tensor_accounting_ok);
    CHECK(report.memory_accounting_ok);
    CHECK(report.benchmark.passed);
    CHECK(report.heap_growth > 0);
    CHECK(report.heap_after < early_heap_capacity());
}

int main(void) {
    printf("TAP version 13\n");
    test_tensor_accounting();
    test_graph_validation();
    test_scheduler();
    test_deadlock_detection();
    test_capabilities();
    test_memory_metrics();
    test_benchmark();
    test_phase14_suite();
    printf("1..%u\n", tests_run);
    printf("Phase 14 host verification: %s (%u/%u passed)\n",
           tests_failed == 0 ? "PASS" : "FAIL",
           tests_run - tests_failed,
           tests_run);
    return tests_failed == 0 ? 0 : 1;
}
