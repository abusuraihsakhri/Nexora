#include <ai/device.h>
#include <ai/instrument.h>
#include <ai/reclaim.h>
#include <ai/scheduler.h>
#include <ai/tensor.h>
#include <ai/work.h>
#include <kernel/memory.h>
#include <kernel/memory_object.h>
#include <kernel/object.h>
#include <kernel/panic.h>

#include <stdio.h>
#include <stdlib.h>

#define TEST_ARENA_SIZE (4u * 1024u * 1024u)

static u8 test_arena[TEST_ARENA_SIZE] __attribute__((aligned(4096)));
static usize test_offset;

void early_heap_init(void) { test_offset = 0; }

void *kalloc_try(usize size, usize alignment) {
    if (alignment == 0) alignment = 1;
    if ((alignment & (alignment - 1u)) != 0) return NULL;
    usize aligned = (test_offset + alignment - 1u) & ~(alignment - 1u);
    if (size > TEST_ARENA_SIZE || aligned > TEST_ARENA_SIZE - size) return NULL;
    void *ptr = &test_arena[aligned];
    test_offset = aligned + size;
    return ptr;
}

void *kalloc(usize size, usize alignment) {
    void *ptr = kalloc_try(size, alignment);
    if (ptr == NULL) panic("test arena exhausted");
    return ptr;
}

usize early_heap_used(void) { return test_offset; }
usize early_heap_capacity(void) { return TEST_ARENA_SIZE; }
usize early_heap_remaining(void) { return TEST_ARENA_SIZE - test_offset; }

void panic(const char *message) {
    fprintf(stderr, "panic: %s\n", message ? message : "(null)");
    exit(2);
}

static void check(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

typedef struct {
    u64 peak_resident;
    u64 final_resident;
    u64 early_reclaimed;
    u64 trace_events;
    u64 trace_peak;
} scenario_result;

static ai_tensor *make_tensor(
    const char *name,
    ai_tensor_class tensor_class,
    ai_tensor_lifetime lifetime,
    u64 elements
) {
    u64 shape[1] = {elements};
    return ai_tensor_create_with_lifetime(
        name,
        AI_DTYPE_F32,
        tensor_class,
        1,
        shape,
        AI_LOC_CPU_RAM,
        lifetime,
        AI_TENSOR_ZERO_INIT
    );
}

static void allocate_or_fail(ai_tensor *tensor) {
    check(ai_tensor_allocate_backing(tensor, NX_MEMORY_PAGE_SIZE) == AI_TENSOR_OK,
          "lazy backing allocation");
}

static scenario_result run_scenario(bool graph_aware) {
    early_heap_init();
    nx_object_system_init();
    ai_device_system_init();
    nx_memory_system_init();
    ai_tensor_system_init();
    ai_reclaim_system_init();
    ai_instrument_system_init();
    ai_reclaim_set_final_consumer_enabled(graph_aware);

    /* Exact page-multiple logical sizes: 96, 128, 160, 160 and 64 KiB. */
    ai_tensor *input = make_tensor(
        "demo-input", AI_TENSOR_CLASS_INPUT, AI_TENSOR_LIFETIME_TEMPORARY, 24576
    );
    ai_tensor *weights = make_tensor(
        "demo-weights", AI_TENSOR_CLASS_WEIGHT, AI_TENSOR_LIFETIME_PERSISTENT, 32768
    );
    ai_tensor *hidden1 = make_tensor(
        "demo-hidden-1", AI_TENSOR_CLASS_ACTIVATION, AI_TENSOR_LIFETIME_TEMPORARY, 40960
    );
    ai_tensor *hidden2 = make_tensor(
        "demo-hidden-2", AI_TENSOR_CLASS_ACTIVATION, AI_TENSOR_LIFETIME_TEMPORARY, 40960
    );
    ai_tensor *output = make_tensor(
        "demo-output", AI_TENSOR_CLASS_OUTPUT, AI_TENSOR_LIFETIME_PERSISTENT, 16384
    );

    allocate_or_fail(input);
    allocate_or_fail(weights);
    ai_trace_memory_sample(1);

    ai_work_graph graph;
    ai_work_graph_init(&graph);

    ai_work_node *stage1 = ai_work_add(
        &graph, "stage-1", AI_OP_MATMUL, 30, AI_WORK_NO_DEADLINE, AI_DEVICE_CPU
    );
    ai_work_add_input(stage1, input);
    ai_work_add_input(stage1, weights);
    ai_work_add_output(stage1, hidden1);

    ai_work_node *stage2 = ai_work_add(
        &graph, "stage-2", AI_OP_ACTIVATION, 20, AI_WORK_NO_DEADLINE, AI_DEVICE_CPU
    );
    ai_work_add_input(stage2, hidden1);
    ai_work_add_input(stage2, weights);
    ai_work_add_output(stage2, hidden2);

    ai_work_node *stage3 = ai_work_add(
        &graph, "stage-3", AI_OP_MATMUL, 10, AI_WORK_NO_DEADLINE, AI_DEVICE_CPU
    );
    ai_work_add_input(stage3, hidden2);
    ai_work_add_input(stage3, weights);
    ai_work_add_output(stage3, output);

    ai_scheduler scheduler;
    ai_scheduler_init(&scheduler, &graph);

    for (u32 step = 0; step < 3; ++step) {
        ai_dispatch_choice choice;
        check(ai_scheduler_pick_dispatch(&scheduler, &choice) == AI_PLACEMENT_OK,
              "scheduler found runnable demo work");
        ai_work_node *node = choice.work;
        check(node != NULL && node->output_count == 1, "demo work has one output");

        ai_tensor *produced = node->outputs[0];
        if (!ai_tensor_is_backed(produced)) {
            allocate_or_fail(produced);
        }
        ai_trace_memory_sample(10u + step);

        check(ai_scheduler_mark_running_on(&scheduler, node, choice.device_id) == AI_WORK_OK,
              "mark demo work running");
        check(ai_scheduler_complete(&scheduler, node) == AI_WORK_OK,
              "complete demo work");
        ai_trace_memory_sample(20u + step);
    }

    scenario_result result;
    result.peak_resident = nx_memory_get_stats()->peak_resident_bytes;
    result.final_resident = nx_memory_get_stats()->resident_bytes;
    result.early_reclaimed = ai_lifetime_get_stats()->resident_bytes_reclaimed;
    result.trace_events = ai_trace_count();
    result.trace_peak = ai_instrument_get_stats()->sampled_peak_resident_bytes;

    check(stage1->state == AI_WORK_DONE && stage2->state == AI_WORK_DONE &&
          stage3->state == AI_WORK_DONE, "all demo work completed");
    check(output->residency == AI_TENSOR_RESIDENCY_RESIDENT,
          "persistent output remains resident");
    check(weights->residency == AI_TENSOR_RESIDENCY_RESIDENT,
          "persistent weights remain resident");

    if (graph_aware) {
        check(input->residency == AI_TENSOR_RESIDENCY_RECLAIMED,
              "graph-aware run reclaimed input");
        check(hidden1->residency == AI_TENSOR_RESIDENCY_RECLAIMED,
              "graph-aware run reclaimed hidden1");
        check(hidden2->residency == AI_TENSOR_RESIDENCY_RECLAIMED,
              "graph-aware run reclaimed hidden2");
    } else {
        check(ai_tensor_is_backed(input) && ai_tensor_is_backed(hidden1) &&
              ai_tensor_is_backed(hidden2),
              "baseline keeps dead intermediates resident");
    }

    ai_work_graph_destroy(&graph);
    check(ai_tensor_release(input), "release input");
    check(ai_tensor_release(weights), "release weights");
    check(ai_tensor_release(hidden1), "release hidden1");
    check(ai_tensor_release(hidden2), "release hidden2");
    check(ai_tensor_release(output), "release output");
    check(nx_memory_get_stats()->resident_bytes == 0, "scenario cleanup drains live memory");

    return result;
}

int main(void) {
    const scenario_result baseline = run_scenario(false);
    const scenario_result aware = run_scenario(true);

    check(baseline.peak_resident > aware.peak_resident,
          "graph-aware lifetime knowledge reduces peak resident memory");
    check(aware.early_reclaimed > 0, "graph-aware run reclaimed memory early");
    check(baseline.early_reclaimed == 0, "baseline has no final-consumer reclaim");
    check(aware.trace_events > 0 && aware.trace_peak == aware.peak_resident,
          "trace captured the resident-memory high water mark");

    const u64 saved = baseline.peak_resident - aware.peak_resident;
    const u64 basis_points = (saved * 10000ull) / baseline.peak_resident;

    printf("phase3 instrumentation/demo tests: PASS\n");
    printf("baseline peak resident: %llu bytes\n",
           (unsigned long long)baseline.peak_resident);
    printf("graph-aware peak resident: %llu bytes\n",
           (unsigned long long)aware.peak_resident);
    printf("peak reduction: %llu bytes (%llu.%02llu%%)\n",
           (unsigned long long)saved,
           (unsigned long long)(basis_points / 100),
           (unsigned long long)(basis_points % 100));
    printf("graph-aware early reclaimed: %llu bytes\n",
           (unsigned long long)aware.early_reclaimed);
    printf("graph-aware final resident: %llu bytes\n",
           (unsigned long long)aware.final_resident);
    printf("trace events: %llu\n", (unsigned long long)aware.trace_events);
    return 0;
}
