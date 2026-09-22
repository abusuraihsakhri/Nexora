#include <ai/integration.h>
#include <ai/capability.h>
#include <ai/scheduler.h>
#include <ai/tensor.h>
#include <ai/work.h>
#include <kernel/memory.h>

static void check(ai_phase14_report *report, bool condition) {
    report->assertions++;
    if (condition) report->assertions_passed++;
    else report->assertions_failed++;
}

static bool tensor_accounting_case(ai_phase14_report *report) {
    ai_tensor_system_init();
    u64 a_shape[2] = {2, 4};
    u64 b_shape[2] = {4, 4};

    ai_tensor *a = ai_tensor_create(
        "phase14-a", AI_DTYPE_F16, 2, a_shape,
        AI_LOC_CPU_RAM, AI_TENSOR_EPHEMERAL
    );
    ai_tensor *b = ai_tensor_create(
        "phase14-b", AI_DTYPE_F32, 2, b_shape,
        AI_LOC_GPU_HBM, AI_TENSOR_PERSISTENT | AI_TENSOR_READONLY
    );

    bool ok = true;
    ok = ok && ai_tensor_validate(a);
    ok = ok && ai_tensor_validate(b);
    ok = ok && ai_tensor_count() == 2;
    ok = ok && a->bytes == 16;
    ok = ok && b->bytes == 64;
    ok = ok && ai_tensor_total_bytes() == 80;
    ok = ok && ai_tensor_bytes_at_location(AI_LOC_CPU_RAM) == 16;
    ok = ok && ai_tensor_bytes_at_location(AI_LOC_GPU_HBM) == 64;

    report->tensor_bytes_accounted += ai_tensor_total_bytes();
    report->tensor_accounting_ok = ok;
    return ok;
}

static bool scheduler_and_graph_case(ai_phase14_report *report) {
    ai_work_graph graph;
    ai_work_graph_init(&graph);

    ai_work_node *low = ai_work_add(
        &graph, "low", AI_OP_NOOP, 10, 2000, AI_DEVICE_CPU
    );
    ai_work_node *high = ai_work_add(
        &graph, "high", AI_OP_NOOP, 100, 3000, AI_DEVICE_CPU
    );
    ai_work_node *after_high = ai_work_add(
        &graph, "after-high", AI_OP_NOOP, 90, 1000, AI_DEVICE_CPU
    );
    ai_work_add_dependency(after_high, high->id);

    ai_graph_validation validation;
    bool valid = ai_work_graph_validate(&graph, &validation);
    if (valid) report->graphs_validated++;

    ai_scheduler scheduler;
    ai_scheduler_init(&scheduler, &graph);

    ai_work_node *first = ai_scheduler_pick(&scheduler);
    bool priority_ok = first == high;
    if (first) {
        ai_scheduler_mark_running(&scheduler, first);
        ai_scheduler_mark_done(&scheduler, first);
    }

    ai_work_node *second = ai_scheduler_pick(&scheduler);
    bool dependency_refresh_ok = second == after_high;
    if (second) {
        ai_scheduler_mark_running(&scheduler, second);
        ai_scheduler_mark_done(&scheduler, second);
    }

    ai_scheduler_run_report run;
    bool complete = ai_scheduler_run_to_completion(&scheduler, &run);
    if (complete) report->graphs_completed++;
    report->scheduler_dispatches += scheduler.dispatch_count;

    return valid && validation.edge_count == 1 && priority_ok &&
           dependency_refresh_ok && complete &&
           ai_work_graph_done_count(&graph) == 3 && low->state == AI_WORK_DONE;
}

static bool deadlock_case(ai_phase14_report *report) {
    ai_work_graph graph;
    ai_work_graph_init(&graph);
    ai_work_node *a = ai_work_add(
        &graph, "cycle-a", AI_OP_CUSTOM, 1, 100, AI_DEVICE_CPU
    );
    ai_work_node *b = ai_work_add(
        &graph, "cycle-b", AI_OP_CUSTOM, 1, 100, AI_DEVICE_CPU
    );
    ai_work_add_dependency(a, b->id);
    ai_work_add_dependency(b, a->id);

    ai_graph_validation validation;
    bool validator_rejects = !ai_work_graph_validate(&graph, &validation) &&
                             validation.cycle_nodes == 2;

    ai_scheduler scheduler;
    ai_scheduler_run_report run;
    ai_scheduler_init(&scheduler, &graph);
    bool scheduler_rejects = !ai_scheduler_run_to_completion(&scheduler, &run) &&
                             run.deadlocked;
    if (scheduler_rejects) report->deadlocks_detected++;

    report->deadlock_detection_ok = validator_rejects && scheduler_rejects;
    return report->deadlock_detection_ok;
}

static bool capability_case(ai_phase14_report *report) {
    ai_capability agent = ai_cap_create(
        42,
        AI_CAP_TENSOR_READ | AI_CAP_MODEL_USE | AI_CAP_GPU_USE
    );

    bool ok = ai_cap_has(&agent, AI_CAP_TENSOR_READ) &&
              ai_cap_has(&agent, AI_CAP_MODEL_USE) &&
              ai_cap_has(&agent, AI_CAP_GPU_USE) &&
              !ai_cap_has(&agent, AI_CAP_TENSOR_WRITE) &&
              !ai_cap_has(&agent, AI_CAP_NETWORK) &&
              !ai_cap_has(&agent, AI_CAP_ADMIN);
    report->capability_isolation_ok = ok;
    return ok;
}

static bool stress_round(ai_phase14_report *report, u32 round) {
    ai_tensor_system_init();
    u64 shape[2] = {1, 64 + (u64)(round % 8u) * 16ull};

    ai_tensor *input = ai_tensor_create(
        "stress-input", AI_DTYPE_F16, 2, shape,
        AI_LOC_CPU_RAM, AI_TENSOR_EPHEMERAL
    );
    ai_tensor *hidden = ai_tensor_create(
        "stress-hidden", AI_DTYPE_F16, 2, shape,
        AI_LOC_GPU_HBM, AI_TENSOR_EPHEMERAL
    );
    ai_tensor *output = ai_tensor_create(
        "stress-output", AI_DTYPE_F16, 2, shape,
        AI_LOC_GPU_HBM, AI_TENSOR_EPHEMERAL
    );

    ai_work_graph graph;
    ai_work_graph_init(&graph);
    ai_work_node *load = ai_work_add(
        &graph, "stress-load", AI_OP_TRANSFER, 80, 3000,
        AI_DEVICE_CPU | AI_DEVICE_GPU
    );
    ai_work_add_input(load, input);
    ai_work_add_output(load, hidden);

    ai_work_node *compute = ai_work_add(
        &graph, "stress-compute", AI_OP_MATMUL, 100, 2000,
        AI_DEVICE_GPU
    );
    ai_work_add_dependency(compute, load->id);
    ai_work_add_input(compute, hidden);
    ai_work_add_output(compute, output);

    ai_work_node *finish = ai_work_add(
        &graph, "stress-finish", AI_OP_ACTIVATION, 90, 1000,
        AI_DEVICE_GPU
    );
    ai_work_add_dependency(finish, compute->id);
    ai_work_add_input(finish, output);

    ai_graph_validation validation;
    if (!ai_work_graph_validate(&graph, &validation)) return false;
    report->graphs_validated++;

    ai_scheduler scheduler;
    ai_scheduler_run_report run;
    ai_scheduler_init(&scheduler, &graph);
    if (!ai_scheduler_run_to_completion(&scheduler, &run)) return false;
    if (run.completed_nodes != 3 || run.deadlocked) return false;

    report->graphs_completed++;
    report->scheduler_dispatches += run.dispatches;
    report->tensor_bytes_accounted += ai_tensor_total_bytes();
    return true;
}

bool ai_phase14_run(ai_phase14_report *report) {
    if (!report) return false;

    report->passed = false;
    report->assertions = 0;
    report->assertions_passed = 0;
    report->assertions_failed = 0;
    report->graphs_validated = 0;
    report->graphs_completed = 0;
    report->deadlocks_detected = 0;
    report->stress_rounds_completed = 0;
    report->scheduler_dispatches = 0;
    report->tensor_bytes_accounted = 0;
    report->heap_before = early_heap_used();
    report->heap_after = 0;
    report->heap_growth = 0;
    report->capability_isolation_ok = false;
    report->deadlock_detection_ok = false;
    report->tensor_accounting_ok = false;
    report->memory_accounting_ok = false;

    check(report, tensor_accounting_case(report));
    check(report, scheduler_and_graph_case(report));
    check(report, deadlock_case(report));
    check(report, capability_case(report));

    for (u32 round = 0; round < AI_PHASE14_STRESS_ROUNDS; ++round) {
        bool ok = stress_round(report, round);
        check(report, ok);
        if (ok) report->stress_rounds_completed++;
    }

    check(report, ai_benchmark_run(&report->benchmark));

    early_heap_stats stats;
    early_heap_get_stats(&stats);
    report->heap_after = stats.used;
    report->heap_growth = report->heap_after - report->heap_before;
    report->memory_accounting_ok =
        stats.used == stats.high_watermark &&
        stats.used <= stats.capacity &&
        stats.requested_bytes <= stats.used &&
        stats.padding_bytes <= stats.used;
    check(report, report->memory_accounting_ok);
    check(report, early_heap_can_alloc(64, 16));

    report->passed =
        report->assertions_failed == 0 &&
        report->stress_rounds_completed == AI_PHASE14_STRESS_ROUNDS &&
        report->benchmark.passed;
    return report->passed;
}
