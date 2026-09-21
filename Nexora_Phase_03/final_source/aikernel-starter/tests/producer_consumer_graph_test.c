#include <ai/tensor.h>
#include <ai/lifetime.h>
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

static ai_tensor *make_tensor(
    const char *name,
    ai_tensor_class tensor_class,
    ai_tensor_lifetime lifetime,
    u32 flags
) {
    u64 shape[1] = {64};
    ai_tensor *tensor = ai_tensor_create_with_lifetime(
        name,
        AI_DTYPE_F32,
        tensor_class,
        1,
        shape,
        AI_LOC_CPU_RAM,
        lifetime,
        flags
    );
    check(ai_tensor_allocate_backing(tensor, NX_MEMORY_PAGE_SIZE) == AI_TENSOR_OK,
          "tensor backing allocation");
    return tensor;
}

int main(void) {
    early_heap_init();
    nx_object_system_init();
    nx_memory_system_init();
    ai_tensor_system_init();

    ai_tensor *source = make_tensor(
        "source", AI_TENSOR_CLASS_INPUT, AI_TENSOR_LIFETIME_TEMPORARY, AI_TENSOR_ZERO_INIT
    );
    ai_tensor *weight = make_tensor(
        "weight", AI_TENSOR_CLASS_WEIGHT, AI_TENSOR_LIFETIME_PERSISTENT, AI_TENSOR_READONLY
    );
    ai_tensor *hidden = make_tensor(
        "hidden", AI_TENSOR_CLASS_ACTIVATION, AI_TENSOR_LIFETIME_TEMPORARY, AI_TENSOR_ZERO_INIT
    );
    ai_tensor *out1 = make_tensor(
        "out1", AI_TENSOR_CLASS_OUTPUT, AI_TENSOR_LIFETIME_TEMPORARY, AI_TENSOR_ZERO_INIT
    );
    ai_tensor *out2 = make_tensor(
        "out2", AI_TENSOR_CLASS_OUTPUT, AI_TENSOR_LIFETIME_TEMPORARY, AI_TENSOR_ZERO_INIT
    );

    const u64 initial_resident = nx_memory_get_stats()->resident_bytes;
    check(initial_resident == 5 * NX_MEMORY_PAGE_SIZE,
          "five page-rounded tensor backings resident");

    ai_work_graph graph;
    ai_work_graph_init(&graph);

    ai_work_node *producer = ai_work_add(
        &graph, "producer", AI_OP_MATMUL, 100, 1000, AI_DEVICE_CPU
    );
    ai_work_add_input(producer, source);
    ai_work_add_input(producer, weight);
    ai_work_add_output(producer, hidden);

    ai_work_node *consumer1 = ai_work_add(
        &graph, "consumer-1", AI_OP_ACTIVATION, 90, 2000, AI_DEVICE_CPU
    );
    ai_work_add_input(consumer1, hidden);
    ai_work_add_output(consumer1, out1);

    ai_work_node *consumer2 = ai_work_add(
        &graph, "consumer-2", AI_OP_NORMALIZATION, 80, 3000, AI_DEVICE_CPU
    );
    ai_work_add_input(consumer2, hidden);
    ai_work_add_output(consumer2, out2);

    check(ai_tensor_producer_work(hidden) == producer->object.handle,
          "hidden records producer work handle");
    check(ai_tensor_consumer_count(hidden) == 2,
          "hidden records two unique consumers");
    check(ai_tensor_consumers_remaining(hidden) == 2,
          "hidden starts with two remaining consumers");
    check(ai_tensor_has_consumer(hidden, consumer1->object.handle),
          "hidden contains consumer1");
    check(ai_tensor_has_consumer(hidden, consumer2->object.handle),
          "hidden contains consumer2");

    check(ai_work_try_add_input(consumer1, hidden) == AI_WORK_ERR_DUPLICATE_INPUT,
          "duplicate consumer edge rejected");

    ai_work_node *conflict = ai_work_add(
        &graph, "conflicting-producer", AI_OP_CUSTOM, 1, 9999, AI_DEVICE_CPU
    );
    check(ai_work_try_add_output(conflict, hidden) == AI_WORK_ERR_PRODUCER_EXISTS,
          "second producer rejected");

    ai_scheduler scheduler;
    ai_scheduler_init(&scheduler, &graph);
    check(producer->state == AI_WORK_READY,
          "root producer becomes ready");
    check(consumer1->state == AI_WORK_PENDING && consumer2->state == AI_WORK_PENDING,
          "tensor consumers wait for producer without explicit dependency IDs");

    ai_work_node *selected = ai_scheduler_pick(&scheduler);
    check(selected == producer, "scheduler selects dataflow root");
    ai_scheduler_mark_running(&scheduler, selected);
    check(ai_scheduler_complete(&scheduler, selected) == AI_WORK_OK,
          "producer completion succeeds");

    check(source->residency == AI_TENSOR_RESIDENCY_RECLAIMED,
          "temporary source reclaimed at its final consumer");
    check(weight->residency == AI_TENSOR_RESIDENCY_RESIDENT,
          "persistent weight survives final-consumer event");
    check(hidden->residency == AI_TENSOR_RESIDENCY_RESIDENT,
          "produced tensor retained while consumers remain");
    check(ai_tensor_consumers_remaining(hidden) == 2,
          "producer completion does not consume its own output");

    selected = ai_scheduler_pick(&scheduler);
    check(selected == consumer1, "higher-priority downstream consumer selected first");
    ai_scheduler_mark_running(&scheduler, selected);
    check(ai_scheduler_complete(&scheduler, selected) == AI_WORK_OK,
          "first consumer completion succeeds");
    check(ai_tensor_consumers_remaining(hidden) == 1,
          "one hidden consumer remains");
    check(ai_tensor_consumer_done(hidden, consumer1->object.handle),
          "consumer1 completion recorded");
    check(hidden->residency == AI_TENSOR_RESIDENCY_RESIDENT,
          "hidden remains resident before final consumer");

    selected = ai_scheduler_pick(&scheduler);
    check(selected == consumer2, "second downstream consumer selected");
    ai_scheduler_mark_running(&scheduler, selected);
    check(ai_scheduler_complete(&scheduler, selected) == AI_WORK_OK,
          "second consumer completion succeeds");
    check(ai_tensor_consumers_remaining(hidden) == 0,
          "hidden reaches zero remaining consumers");
    check(ai_tensor_consumer_done(hidden, consumer2->object.handle),
          "consumer2 completion recorded");
    check(hidden->residency == AI_TENSOR_RESIDENCY_RECLAIMED,
          "hidden automatically reclaimed after final consumer");

    const ai_work_graph_stats *graph_stats = ai_work_graph_get_stats(&graph);
    check(graph_stats != NULL, "graph statistics available");
    check(graph_stats->nodes_created == 4, "graph node creation accounting");
    check(graph_stats->producer_links == 3, "producer-link accounting");
    check(graph_stats->consumer_links == 4, "consumer-link accounting");
    check(graph_stats->consumer_completions == 4, "consumer completion accounting");
    check(graph_stats->final_consumer_events == 3, "final-consumer event accounting");
    check(graph_stats->automatic_reclaim_attempts == 2,
          "automatic reclaim attempted only for temporary resident inputs");
    check(graph_stats->automatic_reclaim_successes == 2,
          "automatic final-consumer reclaim successes");
    check(graph_stats->automatic_reclaim_deferred == 0,
          "no automatic reclaim deferrals");

    check(nx_memory_get_stats()->resident_bytes == 3 * NX_MEMORY_PAGE_SIZE,
          "two temporary backings removed from live resident accounting");

    ai_work_graph_destroy(&graph);
    check(graph.node_count == 0, "graph destruction releases work objects");
    check(hidden->producer_work == NX_INVALID_HANDLE,
          "work destruction clears producer provenance");
    check(hidden->consumer_count == 0 && hidden->consumers_remaining == 0,
          "work destruction clears consumer provenance");

    check(ai_tensor_release(source), "release source creator reference");
    check(ai_tensor_release(weight), "release weight creator reference");
    check(ai_tensor_release(hidden), "release hidden creator reference");
    check(ai_tensor_release(out1), "release out1 creator reference");
    check(ai_tensor_release(out2), "release out2 creator reference");

    check(ai_tensor_count() == 0, "all tensor creator references released");
    check(nx_memory_get_stats()->resident_bytes == 0,
          "all live memory objects drained after tensor destruction");

    puts("producer/consumer graph tests: PASS");
    return 0;
}
