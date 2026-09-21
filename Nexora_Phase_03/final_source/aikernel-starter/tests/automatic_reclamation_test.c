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

static ai_tensor *make_tensor(
    const char *name,
    ai_tensor_lifetime lifetime,
    u64 elements,
    u32 flags
) {
    u64 shape[1] = {elements};
    ai_tensor *tensor = ai_tensor_create_with_lifetime(
        name,
        AI_DTYPE_F32,
        AI_TENSOR_CLASS_ACTIVATION,
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
    ai_reclaim_system_init();

    /* Final-consumer reclaim must defer while a transient object pin exists. */
    ai_tensor *deferred_input = make_tensor(
        "deferred-input", AI_TENSOR_LIFETIME_TEMPORARY, 64, AI_TENSOR_ZERO_INIT
    );
    ai_work_graph deferred_graph;
    ai_work_graph_init(&deferred_graph);
    ai_work_node *deferred_consumer = ai_work_add(
        &deferred_graph, "deferred-consumer", AI_OP_CUSTOM, 10, 0, AI_DEVICE_CPU
    );
    ai_work_add_input(deferred_consumer, deferred_input);
    ai_work_refresh_states(&deferred_graph);
    check(deferred_consumer->state == AI_WORK_READY, "deferred consumer ready");
    check(ai_tensor_pin(deferred_input), "transient tensor pin");
    deferred_consumer->state = AI_WORK_RUNNING;
    check(ai_work_complete(deferred_consumer) == AI_WORK_OK,
          "completion succeeds even when reclaim defers");
    check(ai_tensor_is_backed(deferred_input), "pinned final-consumer input remains backed");
    check(ai_reclaim_deferred_count() == 1, "retryable reclaim queued");
    check(deferred_graph.stats.automatic_reclaim_deferred == 1,
          "graph records reclaim deferral");

    check(ai_tensor_unpin(deferred_input), "drop transient tensor pin");
    ai_scheduler deferred_scheduler;
    ai_scheduler_init(&deferred_scheduler, &deferred_graph);
    check(ai_scheduler_pick(&deferred_scheduler) == NULL,
          "scheduler reaches retry point with no remaining work");
    check(ai_reclaim_deferred_count() == 0, "scheduler retry drains deferred queue");
    check(deferred_input->residency == AI_TENSOR_RESIDENCY_RECLAIMED,
          "deferred tensor becomes reclaimed");
    ai_work_graph_destroy(&deferred_graph);

    /* Persistent and active tensors must survive pressure. */
    ai_tensor *persistent = make_tensor(
        "persistent", AI_TENSOR_LIFETIME_PERSISTENT, 64, AI_TENSOR_READONLY
    );
    ai_tensor *active = make_tensor(
        "active", AI_TENSOR_LIFETIME_TEMPORARY, 64, AI_TENSOR_ZERO_INIT
    );
    ai_work_graph active_graph;
    ai_work_graph_init(&active_graph);
    ai_work_node *active_consumer = ai_work_add(
        &active_graph, "active-consumer", AI_OP_CUSTOM, 5, 0, AI_DEVICE_CPU
    );
    ai_work_add_input(active_consumer, active);
    ai_work_refresh_states(&active_graph);
    check(active->consumers_remaining == 1, "active tensor has pending consumer");

    /* A completed producer with no consumers leaves a graph-dead temporary output. */
    ai_tensor *dead_output = make_tensor(
        "dead-output", AI_TENSOR_LIFETIME_TEMPORARY, 2048, AI_TENSOR_ZERO_INIT
    );
    ai_work_graph dead_graph;
    ai_work_graph_init(&dead_graph);
    ai_work_node *dead_producer = ai_work_add(
        &dead_graph, "dead-producer", AI_OP_CUSTOM, 5, 0, AI_DEVICE_CPU
    );
    ai_work_add_output(dead_producer, dead_output);
    ai_work_refresh_states(&dead_graph);
    dead_producer->state = AI_WORK_RUNNING;
    check(ai_work_complete(dead_producer) == AI_WORK_OK, "dead producer completes");
    check(ai_tensor_is_backed(dead_output), "unused output remains until pressure reclaim");

    /* Cache-only tensors are evictable; larger cache should be selected first. */
    ai_tensor *cache_large = make_tensor(
        "cache-large", AI_TENSOR_LIFETIME_CACHED, 2048, AI_TENSOR_ZERO_INIT
    );
    ai_tensor *cache_small = make_tensor(
        "cache-small", AI_TENSOR_LIFETIME_CACHED, 1024, AI_TENSOR_ZERO_INIT
    );

    const u64 resident_before = nx_memory_get_stats()->resident_bytes;
    ai_reclaim_report report;
    check(ai_reclaim_under_pressure(3u * NX_MEMORY_PAGE_SIZE, &report) == AI_RECLAIM_OK,
          "pressure target satisfied");
    check(report.target_met, "pressure report target met");
    check(report.resident_bytes_reclaimed >= 3u * NX_MEMORY_PAGE_SIZE,
          "pressure report measures actual resident bytes");
    check(report.resident_before == resident_before,
          "pressure report records resident baseline");
    check(dead_output->residency == AI_TENSOR_RESIDENCY_RECLAIMED,
          "graph-dead temporary reclaimed before cache");
    check(cache_large->residency == AI_TENSOR_RESIDENCY_RECLAIMED,
          "largest safe cache evicted to satisfy pressure");
    check(ai_tensor_is_backed(active), "active tensor protected from pressure");
    check(ai_tensor_is_backed(persistent), "persistent tensor protected from pressure");
    check(ai_tensor_is_backed(cache_small), "pressure stops after target is met");

    ai_reclaim_report cache_report;
    check(ai_reclaim_evict_cache(NX_MEMORY_PAGE_SIZE, &cache_report) == AI_RECLAIM_OK,
          "explicit cache eviction succeeds");
    check(cache_small->residency == AI_TENSOR_RESIDENCY_RECLAIMED,
          "remaining cache evicted");

    /* Completing the protected active consumer returns it to final-use reclaim. */
    active_consumer->state = AI_WORK_RUNNING;
    check(ai_work_complete(active_consumer) == AI_WORK_OK, "active consumer completes");
    check(active->residency == AI_TENSOR_RESIDENCY_RECLAIMED,
          "active temporary reclaimed at final consumer");

    const ai_reclaim_stats *reclaim_stats = ai_reclaim_get_stats();
    check(reclaim_stats->final_consumer_deferred >= 1,
          "global final-consumer deferral counted");
    check(reclaim_stats->deferred_retry_successes >= 1,
          "global deferred retry success counted");
    check(reclaim_stats->pressure_runs == 1, "pressure run counted");
    check(reclaim_stats->pressure_target_met == 1, "pressure target success counted");
    check(reclaim_stats->cache_eviction_runs == 1, "cache eviction run counted");

    ai_work_graph_destroy(&active_graph);
    ai_work_graph_destroy(&dead_graph);

    check(ai_tensor_release(deferred_input), "release deferred input");
    check(ai_tensor_release(persistent), "release persistent");
    check(ai_tensor_release(active), "release active");
    check(ai_tensor_release(dead_output), "release dead output");
    check(ai_tensor_release(cache_large), "release large cache");
    check(ai_tensor_release(cache_small), "release small cache");

    check(ai_tensor_count() == 0, "all tensors released");
    check(nx_memory_get_stats()->resident_bytes == 0,
          "all live resident memory drained at test end");

    puts("automatic reclamation tests: PASS");
    return 0;
}
