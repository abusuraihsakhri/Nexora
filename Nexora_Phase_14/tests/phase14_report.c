#include <stdio.h>
#include <ai/integration.h>
#include <ai/tensor.h>
#include <kernel/memory.h>

int main(void) {
    early_heap_init();
    ai_tensor_system_init();

    ai_phase14_report r;
    bool ok = ai_phase14_run(&r);

    printf("{\n");
    printf("  \"phase\": 14,\n");
    printf("  \"status\": \"%s\",\n", ok ? "PASS" : "FAIL");
    printf("  \"assertions\": {\"passed\": %u, \"failed\": %u, \"total\": %u},\n",
           r.assertions_passed, r.assertions_failed, r.assertions);
    printf("  \"stress\": {\"rounds_completed\": %u, \"rounds_requested\": %u},\n",
           r.stress_rounds_completed, AI_PHASE14_STRESS_ROUNDS);
    printf("  \"graphs\": {\"validated\": %u, \"completed\": %u, \"deadlocks_detected\": %u},\n",
           r.graphs_validated, r.graphs_completed, r.deadlocks_detected);
    printf("  \"scheduler_dispatches\": %llu,\n",
           (unsigned long long)r.scheduler_dispatches);
    printf("  \"tensor_bytes_accounted\": %llu,\n",
           (unsigned long long)r.tensor_bytes_accounted);
    printf("  \"heap\": {\"before\": %llu, \"after\": %llu, \"growth\": %llu},\n",
           (unsigned long long)r.heap_before,
           (unsigned long long)r.heap_after,
           (unsigned long long)r.heap_growth);
    printf("  \"checks\": {\n");
    printf("    \"capability_isolation\": %s,\n", r.capability_isolation_ok ? "true" : "false");
    printf("    \"deadlock_detection\": %s,\n", r.deadlock_detection_ok ? "true" : "false");
    printf("    \"tensor_accounting\": %s,\n", r.tensor_accounting_ok ? "true" : "false");
    printf("    \"memory_accounting\": %s\n", r.memory_accounting_ok ? "true" : "false");
    printf("  },\n");
    printf("  \"benchmark\": {\n");
    printf("    \"status\": \"%s\",\n", r.benchmark.passed ? "PASS" : "FAIL");
    printf("    \"scenarios_run\": %u,\n", r.benchmark.scenarios_run);
    printf("    \"scenarios_completed\": %u,\n", r.benchmark.scenarios_completed);
    printf("    \"nodes_total\": %u,\n", r.benchmark.nodes_total);
    printf("    \"edges_total\": %u,\n", r.benchmark.edges_total);
    printf("    \"dispatches\": %llu,\n", (unsigned long long)r.benchmark.dispatches);
    printf("    \"picks\": %llu,\n", (unsigned long long)r.benchmark.picks);
    printf("    \"candidates_scanned\": %llu,\n", (unsigned long long)r.benchmark.candidates_scanned);
    printf("    \"selection_efficiency_ppm\": %llu\n",
           (unsigned long long)r.benchmark.selection_efficiency_ppm);
    printf("  }\n");
    printf("}\n");

    return ok ? 0 : 1;
}
