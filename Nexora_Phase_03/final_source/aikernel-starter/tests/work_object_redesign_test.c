#include <ai/tensor.h>
#include <ai/work.h>
#include <ai/scheduler.h>
#include <kernel/memory.h>
#include <kernel/memory_object.h>
#include <kernel/object.h>
#include <kernel/panic.h>

#include <stdio.h>
#include <stdlib.h>

#define TEST_ARENA_SIZE (2u * 1024u * 1024u)

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

static ai_work_desc base_desc(const char *name) {
    ai_work_desc desc;
    desc.name = name;
    desc.op = AI_OP_CUSTOM;
    desc.work_class = AI_WORK_CLASS_COMPUTE;
    desc.qos = AI_WORK_QOS_DEFAULT;
    desc.priority = 50;
    desc.deadline_ns = AI_WORK_NO_DEADLINE;
    desc.device_mask = AI_DEVICE_CPU;
    desc.preferred_device = AI_DEVICE_INVALID;
    desc.estimated_ops = 0;
    desc.estimated_read_bytes = 0;
    desc.estimated_write_bytes = 0;
    desc.estimated_scratch_bytes = 0;
    desc.estimated_duration_ns = 0;
    desc.batch_id = AI_WORK_NO_BATCH;
    desc.batch_size = 1;
    desc.flags = AI_WORK_FLAG_AUTO_IO_BYTES;
    return desc;
}

int main(void) {
    early_heap_init();
    nx_object_system_init();
    nx_memory_system_init();
    ai_tensor_system_init();

    u64 input_shape[2] = {4, 8};
    u64 weight_shape[2] = {8, 16};
    u64 output_shape[2] = {4, 16};

    ai_tensor *input = ai_tensor_create_with_lifetime(
        "input", AI_DTYPE_F32, AI_TENSOR_CLASS_INPUT, 2, input_shape,
        AI_LOC_CPU_RAM, AI_TENSOR_LIFETIME_TEMPORARY, 0
    );
    ai_tensor *weight = ai_tensor_create_with_lifetime(
        "weight", AI_DTYPE_F32, AI_TENSOR_CLASS_WEIGHT, 2, weight_shape,
        AI_LOC_CPU_RAM, AI_TENSOR_LIFETIME_PERSISTENT, AI_TENSOR_READONLY
    );
    ai_tensor *output = ai_tensor_create_with_lifetime(
        "output", AI_DTYPE_F32, AI_TENSOR_CLASS_OUTPUT, 2, output_shape,
        AI_LOC_CPU_RAM, AI_TENSOR_LIFETIME_TEMPORARY, 0
    );

    check(input->logical_bytes == 128, "input logical bytes");
    check(weight->logical_bytes == 512, "weight logical bytes");
    check(output->logical_bytes == 256, "output logical bytes");

    ai_work_graph graph;
    ai_work_graph_init(&graph);

    ai_work_desc desc = base_desc("profiled-matmul");
    desc.op = AI_OP_MATMUL;
    desc.qos = AI_WORK_QOS_LATENCY;
    desc.priority = 120;
    desc.deadline_ns = 5000;
    desc.device_mask = AI_DEVICE_CPU | AI_DEVICE_GPU;
    desc.estimated_ops = 4096;
    desc.estimated_scratch_bytes = 64;
    desc.estimated_duration_ns = 2000;
    desc.batch_id = 77;
    desc.batch_size = 4;
    desc.flags = AI_WORK_FLAG_AUTO_IO_BYTES |
                 AI_WORK_FLAG_PREEMPTIBLE |
                 AI_WORK_FLAG_BATCHABLE |
                 AI_WORK_FLAG_DETERMINISTIC;

    ai_work_node *matmul = NULL;
    check(ai_work_try_create(&graph, &desc, &matmul) == AI_WORK_OK && matmul != NULL,
          "descriptor work creation");
    check(matmul->work_class == AI_WORK_CLASS_COMPUTE, "work class stored");
    check(matmul->qos == AI_WORK_QOS_LATENCY, "qos stored");
    check(matmul->batch_id == 77 && matmul->batch_size == 4, "batch metadata stored");
    check(ai_work_has_deadline(matmul), "deadline recognized");

    check(ai_work_try_add_input(matmul, input) == AI_WORK_OK, "first input attached");
    check(ai_work_try_add_input(matmul, weight) == AI_WORK_OK, "second input attached");
    check(ai_work_try_add_output(matmul, output) == AI_WORK_OK, "output attached");

    check(matmul->logical_input_bytes == 640, "logical input bytes derived");
    check(matmul->logical_output_bytes == 256, "logical output bytes derived");
    check(matmul->estimated_read_bytes == 640, "auto read estimate derived");
    check(matmul->estimated_write_bytes == 256, "auto write estimate derived");
    check(ai_work_logical_total_bytes(matmul) == 896, "logical total bytes");
    check(ai_work_estimated_total_bytes(matmul) == 960,
          "estimated bytes include scratch");
    check(ai_work_is_profiled(matmul), "work profile recognized");

    ai_work_desc invalid = base_desc("invalid-auto-estimate");
    invalid.estimated_read_bytes = 1;
    ai_work_node *invalid_node = NULL;
    check(ai_work_try_create(&graph, &invalid, &invalid_node) == AI_WORK_ERR_INVALID_ESTIMATE,
          "auto io rejects explicit read estimate");
    check(invalid_node == NULL, "invalid estimate does not publish node");

    invalid = base_desc("invalid-batch");
    invalid.batch_size = 2;
    check(ai_work_try_create(&graph, &invalid, &invalid_node) == AI_WORK_ERR_INVALID_BATCH,
          "batch size requires batch identity");

    invalid = base_desc("invalid-device");
    invalid.device_mask = 1u << 20;
    check(ai_work_try_create(&graph, &invalid, &invalid_node) == AI_WORK_ERR_INVALID_DEVICE_MASK,
          "unknown device mask rejected");

    ai_work_desc explicit_desc = base_desc("explicit-traffic");
    explicit_desc.flags = AI_WORK_FLAG_DETERMINISTIC;
    explicit_desc.estimated_read_bytes = 1000;
    explicit_desc.estimated_write_bytes = 500;
    explicit_desc.estimated_scratch_bytes = 250;
    ai_work_node *explicit_node = ai_work_create(&graph, &explicit_desc);
    check(ai_work_try_add_input(explicit_node, input) == AI_WORK_OK,
          "explicit estimate node accepts tensor input");
    check(explicit_node->logical_input_bytes == 128, "explicit node tracks logical input");
    check(explicit_node->estimated_read_bytes == 1000,
          "explicit read estimate not overwritten");
    check(explicit_node->estimated_write_bytes == 500,
          "explicit write estimate not overwritten");
    check(ai_work_estimated_total_bytes(explicit_node) == 1750,
          "explicit total estimate");

    ai_work_graph sched_graph;
    ai_work_graph_init(&sched_graph);

    ai_work_desc no_deadline_desc = base_desc("no-deadline-latency");
    no_deadline_desc.qos = AI_WORK_QOS_LATENCY;
    no_deadline_desc.estimated_duration_ns = 50;
    ai_work_node *no_deadline = ai_work_create(&sched_graph, &no_deadline_desc);

    ai_work_desc deadline_desc = base_desc("deadline-background");
    deadline_desc.qos = AI_WORK_QOS_BACKGROUND;
    deadline_desc.deadline_ns = 1000;
    deadline_desc.estimated_duration_ns = 100;
    ai_work_node *deadline = ai_work_create(&sched_graph, &deadline_desc);

    ai_scheduler scheduler;
    ai_scheduler_init(&scheduler, &sched_graph);
    check(ai_scheduler_pick(&scheduler) == deadline,
          "deadline-bearing work precedes same-priority no-deadline work");
    ai_scheduler_mark_running(&scheduler, deadline);
    check(ai_scheduler_complete(&scheduler, deadline) == AI_WORK_OK,
          "deadline work completion");

    ai_work_desc throughput_desc = base_desc("throughput");
    throughput_desc.qos = AI_WORK_QOS_THROUGHPUT;
    throughput_desc.estimated_duration_ns = 10;
    ai_work_node *throughput = ai_work_create(&sched_graph, &throughput_desc);
    (void)throughput;
    ai_work_refresh_states(&sched_graph);
    check(ai_scheduler_pick(&scheduler) == no_deadline,
          "latency qos precedes throughput qos at equal priority/deadline");
    ai_scheduler_mark_running(&scheduler, no_deadline);
    check(ai_scheduler_complete(&scheduler, no_deadline) == AI_WORK_OK,
          "latency work completion");

    ai_work_desc short_desc = base_desc("short-default");
    short_desc.estimated_duration_ns = 20;
    ai_work_node *short_work = ai_work_create(&sched_graph, &short_desc);
    ai_work_desc long_desc = base_desc("long-default");
    long_desc.estimated_duration_ns = 200;
    ai_work_node *long_work = ai_work_create(&sched_graph, &long_desc);
    (void)long_work;
    ai_work_refresh_states(&sched_graph);

    /* Throughput has lower QoS rank, so DEFAULT work should precede it. */
    check(ai_scheduler_pick(&scheduler) == short_work,
          "default qos precedes throughput and shorter duration breaks default tie");

    ai_work_graph_destroy(&sched_graph);
    ai_work_graph_destroy(&graph);

    check(ai_tensor_release(input), "release input creator ref");
    check(ai_tensor_release(weight), "release weight creator ref");
    check(ai_tensor_release(output), "release output creator ref");

    printf("work-object redesign tests: PASS\n");
    return 0;
}
