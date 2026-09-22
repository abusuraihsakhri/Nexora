#include <ai/benchmark.h>
#include <ai/work.h>
#include <ai/scheduler.h>

static bool run_chain(u32 node_count, ai_benchmark_report *report) {
    ai_work_graph graph;
    ai_work_graph_init(&graph);

    ai_work_node *previous = NULL;
    for (u32 i = 0; i < node_count; ++i) {
        u32 priority = (i % 3u) == 0u ? 100u : (50u + i);
        ai_work_node *node = ai_work_add(
            &graph,
            "benchmark-node",
            AI_OP_CUSTOM,
            priority,
            1000000ull + (u64)(node_count - i) * 1000ull,
            AI_DEVICE_CPU | AI_DEVICE_GPU
        );
        if (previous) ai_work_add_dependency(node, previous->id);
        previous = node;
    }

    ai_graph_validation validation;
    if (!ai_work_graph_validate(&graph, &validation)) return false;

    ai_scheduler scheduler;
    ai_scheduler_run_report run;
    ai_scheduler_init(&scheduler, &graph);
    if (!ai_scheduler_run_to_completion(&scheduler, &run)) return false;

    report->nodes_total += validation.node_count;
    report->edges_total += validation.edge_count;
    report->dispatches += run.dispatches;
    report->picks += run.picks;
    report->candidates_scanned += run.candidates_scanned;
    return run.completed_nodes == node_count;
}

bool ai_benchmark_run(ai_benchmark_report *report) {
    if (!report) return false;

    report->passed = false;
    report->scenarios_run = 0;
    report->scenarios_completed = 0;
    report->nodes_total = 0;
    report->edges_total = 0;
    report->dispatches = 0;
    report->picks = 0;
    report->candidates_scanned = 0;
    report->selection_efficiency_ppm = 0;

    const u32 sizes[] = {4, 8, 16, 32};
    for (u32 i = 0; i < (u32)(sizeof(sizes) / sizeof(sizes[0])); ++i) {
        report->scenarios_run++;
        if (run_chain(sizes[i], report)) {
            report->scenarios_completed++;
        }
    }

    if (report->candidates_scanned != 0) {
        report->selection_efficiency_ppm =
            (report->dispatches * 1000000ull) / report->candidates_scanned;
    }

    report->passed = report->scenarios_completed == report->scenarios_run;
    return report->passed;
}
