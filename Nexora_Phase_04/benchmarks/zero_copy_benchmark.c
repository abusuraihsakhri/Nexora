#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <ai/backing.h>
#include <ai/domain.h>
#include <ai/handle.h>
#include <ai/mapping.h>
#include <ai/tensor.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

#define TEST_ARENA_SIZE (96u * 1024u * 1024u)
#define SHARE_ITERS 20000u
#define MAP_ITERS 64u

static u8 test_arena[TEST_ARENA_SIZE];
static usize test_offset = 0;
static volatile uint64_t benchmark_sink = 0;

void early_heap_init(void) { test_offset = 0; }

void *kalloc(usize size, usize alignment) {
    if (alignment == 0) alignment = 1;
    usize aligned = (test_offset + alignment - 1u) & ~(alignment - 1u);
    if (aligned + size > TEST_ARENA_SIZE) __builtin_trap();
    void *ptr = &test_arena[aligned];
    test_offset = aligned + size;
    return ptr;
}

usize early_heap_used(void) { return test_offset; }
usize early_heap_capacity(void) { return TEST_ARENA_SIZE; }

__attribute__((noreturn))
void panic(const char *message) {
    (void)message;
    __builtin_trap();
}

static uint64_t now_ns(void) {
    struct timespec ts;
#ifdef CLOCK_MONOTONIC_RAW
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
    clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static ai_domain *make_domain(const char *name) {
    ai_domain_limits unlimited = {0, 0, 0};
    ai_domain *domain = ai_domain_create(name, 0, unlimited);
    assert(domain != NULL);
    assert(ai_domain_activate(domain));
    return domain;
}

static void destroy_domain(ai_domain *domain) {
    assert(ai_handle_live_count(domain) == 0);
    assert(ai_mapping_live_count(domain) == 0);
    if (ai_domain_state_get(domain) == AI_DOMAIN_ACTIVE) {
        assert(ai_domain_begin_quiesce(domain));
    }
    assert(ai_domain_destroy(domain));
}

static unsigned copy_iterations_for_size(size_t bytes) {
    const size_t target = 256u * 1024u * 1024u;
    unsigned n = (unsigned)(target / bytes);
    if (n < 8u) n = 8u;
    if (n > 256u) n = 256u;
    return n;
}

static void run_case(size_t bytes) {
    assert((bytes % 4u) == 0);
    ai_domain *producer = make_domain("bench-producer");
    ai_domain *consumer = make_domain("bench-consumer");

    u64 shape[1] = {(u64)(bytes / 4u)};
    ai_tensor *tensor = ai_tensor_create(
        "benchmark-tensor",
        AI_DTYPE_F32,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL);
    assert(tensor != NULL && tensor->bytes == (u64)bytes);

    ai_tensor_backing *backing = ai_backing_create_ram(
        tensor->bytes, AI_BACKING_ZEROED);
    assert(backing != NULL);
    assert(ai_tensor_attach_backing(tensor, backing, 0));

    ai_handle_t owner = ai_handle_install(
        producer,
        tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ |
        AI_HANDLE_RIGHT_WRITE |
        AI_HANDLE_RIGHT_MAP |
        AI_HANDLE_RIGHT_SHARE);
    assert(owner != AI_HANDLE_INVALID);

    ai_mapping_id_t producer_map = ai_tensor_map(
        producer, owner, AI_MAP_PROT_READ | AI_MAP_PROT_WRITE);
    assert(producer_map != AI_MAPPING_INVALID);
    u8 *src = (u8 *)ai_mapping_kernel_address(producer, producer_map);
    assert(src != NULL);

    for (size_t i = 0; i < bytes; i += 4096u) {
        src[i] = (u8)((i / 4096u) ^ 0x5au);
    }
    src[bytes - 1u] = 0xa5u;

    u8 *copy_dst = (u8 *)malloc(bytes);
    assert(copy_dst != NULL);
    memset(copy_dst, 0, bytes);

    unsigned copy_iters = copy_iterations_for_size(bytes);
    memcpy(copy_dst, src, bytes); /* warm */

    uint64_t t0 = now_ns();
    for (unsigned i = 0; i < copy_iters; ++i) {
        memcpy(copy_dst, src, bytes);
        benchmark_sink += copy_dst[(size_t)i % bytes];
    }
    uint64_t copy_elapsed = now_ns() - t0;

    t0 = now_ns();
    for (unsigned i = 0; i < SHARE_ITERS; ++i) {
        ai_handle_t h = ai_handle_share(
            producer,
            owner,
            consumer,
            AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP);
        assert(h != AI_HANDLE_INVALID);
        benchmark_sink += (uint64_t)ai_handle_generation(h);
        assert(ai_handle_close(consumer, h));
    }
    uint64_t share_elapsed = now_ns() - t0;

    t0 = now_ns();
    for (unsigned i = 0; i < MAP_ITERS; ++i) {
        ai_handle_t h = ai_handle_share(
            producer,
            owner,
            consumer,
            AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP);
        assert(h != AI_HANDLE_INVALID);
        ai_mapping_id_t m = ai_tensor_map(consumer, h, AI_MAP_PROT_READ);
        assert(m != AI_MAPPING_INVALID);
        assert(ai_mapping_physical_address(consumer, m) == backing->physical_base);
        const u8 *shared = (const u8 *)ai_mapping_kernel_address(consumer, m);
        assert(shared != NULL);
        benchmark_sink += shared[bytes - 1u];
        assert(ai_tensor_unmap(consumer, m));
        assert(ai_handle_close(consumer, h));
    }
    uint64_t map_elapsed = now_ns() - t0;

    double copy_ns = (double)copy_elapsed / (double)copy_iters;
    double share_ns = (double)share_elapsed / (double)SHARE_ITERS;
    double map_ns = (double)map_elapsed / (double)MAP_ITERS;
    double gib_s = ((double)bytes / (1024.0 * 1024.0 * 1024.0)) /
                   (copy_ns / 1000000000.0);

    printf("%zu,%u,%.1f,%.3f,%u,%.1f,%u,%.1f,%zu,%zu,0\n",
           bytes,
           copy_iters,
           copy_ns,
           gib_s,
           SHARE_ITERS,
           share_ns,
           MAP_ITERS,
           map_ns,
           bytes * 2u,
           bytes);

    free(copy_dst);

    assert(ai_tensor_unmap(producer, producer_map));
    assert(ai_handle_close(producer, owner));
    assert(ai_tensor_put(tensor));
    assert(ai_backing_put(backing));
    assert(!ai_tensor_is_live(tensor));
    assert(!ai_backing_is_live(backing));

    destroy_domain(producer);
    destroy_domain(consumer);
}

int main(void) {
    early_heap_init();
    ai_backing_system_init();
    ai_tensor_system_init();
    ai_domain_system_init();

    puts("bytes,copy_iters,memcpy_ns_per_handoff,memcpy_GiB_s,share_iters,share_handle_ns,share_map_iters,share_plus_map_ns,copy_peak_payload_bytes,shared_peak_payload_bytes,shared_payload_bytes_copied");
    run_case(1u * 1024u * 1024u);
    run_case(4u * 1024u * 1024u);
    run_case(16u * 1024u * 1024u);
    run_case(32u * 1024u * 1024u);

    assert(ai_domain_count() == 0);
    assert(ai_tensor_count() == 0);
    assert(ai_backing_count() == 0);
    fprintf(stderr, "benchmark_sink=%" PRIu64 "\n", (uint64_t)benchmark_sink);
    return 0;
}
