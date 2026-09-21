#include <ai/tensor.h>
#include <ai/lifetime.h>
#include <kernel/memory.h>
#include <kernel/memory_object.h>
#include <kernel/object.h>
#include <kernel/panic.h>

#include <stdio.h>
#include <stdlib.h>

#define TEST_ARENA_SIZE (1024u * 1024u)

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
    u32 flags
) {
    u64 shape[1] = {64};
    return ai_tensor_create_with_lifetime(
        name,
        AI_DTYPE_F32,
        AI_TENSOR_CLASS_ACTIVATION,
        1,
        shape,
        AI_LOC_CPU_RAM,
        lifetime,
        flags
    );
}

int main(void) {
    early_heap_init();
    nx_object_system_init();
    nx_memory_system_init();
    ai_tensor_system_init();

    /* Temporary tensors are eligible immediately after their final consumer. */
    ai_tensor *temporary = make_tensor(
        "temporary", AI_TENSOR_LIFETIME_TEMPORARY, AI_TENSOR_ZERO_INIT
    );
    check(temporary->lifetime == AI_TENSOR_LIFETIME_TEMPORARY,
          "temporary lifetime classification");
    check(temporary->flags & AI_TENSOR_EPHEMERAL,
          "temporary compatibility flag canonicalized");
    check(ai_tensor_allocate_backing(temporary, 64) == AI_TENSOR_OK,
          "temporary backing allocation");
    nx_handle_t temp_memory = temporary->backing_memory;
    check(ai_tensor_lifetime_can_reclaim(temporary, AI_RECLAIM_FINAL_CONSUMER),
          "temporary final-consumer eligibility");
    check(ai_tensor_lifetime_reclaim(temporary, AI_RECLAIM_FINAL_CONSUMER) == AI_LIFETIME_OK,
          "temporary final-consumer reclaim");
    check(temporary->residency == AI_TENSOR_RESIDENCY_RECLAIMED,
          "temporary marked reclaimed");
    check(temporary->reclaim_count == 1,
          "temporary reclaim count");
    check(nx_memory_lookup(temp_memory) == NULL,
          "private temporary backing destroyed");

    /* A reclaimed tensor object can be re-backed without recreating metadata. */
    check(ai_tensor_allocate_backing(temporary, 64) == AI_TENSOR_OK,
          "re-back reclaimed tensor");
    check(temporary->residency == AI_TENSOR_RESIDENCY_RESIDENT,
          "re-backed tensor resident");
    check(ai_tensor_lifetime_reclaim(temporary, AI_RECLAIM_MEMORY_PRESSURE) == AI_LIFETIME_OK,
          "temporary pressure reclaim");

    /* Persistent storage resists automatic/pressure reclaim. */
    ai_tensor *persistent = make_tensor(
        "persistent", AI_TENSOR_LIFETIME_PERSISTENT, AI_TENSOR_READONLY
    );
    check(persistent->flags & AI_TENSOR_PERSISTENT,
          "persistent compatibility flag canonicalized");
    check(ai_tensor_allocate_backing(persistent, 64) == AI_TENSOR_OK,
          "persistent backing allocation");
    check(ai_tensor_lifetime_reclaim(persistent, AI_RECLAIM_MEMORY_PRESSURE) ==
              AI_LIFETIME_ERR_POLICY_DENIED,
          "persistent pressure reclaim denied");
    check(ai_tensor_is_backed(persistent),
          "persistent remains resident after denied reclaim");
    check(ai_tensor_lifetime_reclaim(persistent, AI_RECLAIM_EXPLICIT) == AI_LIFETIME_OK,
          "persistent explicit reclaim allowed");

    /* Cached tensors can be evicted and later repopulated. */
    ai_tensor *cached = make_tensor(
        "cached", AI_TENSOR_LIFETIME_CACHED, AI_TENSOR_ZERO_INIT
    );
    check((cached->flags & (AI_TENSOR_PERSISTENT | AI_TENSOR_EPHEMERAL)) == 0,
          "cached has no legacy lifetime alias");
    check(ai_tensor_allocate_backing(cached, 64) == AI_TENSOR_OK,
          "cached backing allocation");
    check(ai_tensor_lifetime_reclaim(cached, AI_RECLAIM_CACHE_EVICTION) == AI_LIFETIME_OK,
          "cache eviction reclaim");
    check(cached->last_reclaim_reason == AI_RECLAIM_CACHE_EVICTION,
          "cache eviction reason recorded");

    /* Shared tensor metadata must not be stripped while another strong user exists. */
    ai_tensor *shared = make_tensor(
        "shared", AI_TENSOR_LIFETIME_SHARED, AI_TENSOR_ZERO_INIT
    );
    check(ai_tensor_allocate_backing(shared, 64) == AI_TENSOR_OK,
          "shared backing allocation");
    check(ai_tensor_retain(shared), "shared extra reference");
    check(ai_tensor_lifetime_reclaim(shared, AI_RECLAIM_EXPLICIT) ==
              AI_LIFETIME_ERR_SHARED_BUSY,
          "shared reclaim blocked while multiply referenced");
    check(ai_tensor_release(shared), "drop shared extra reference");
    check(ai_tensor_lifetime_reclaim(shared, AI_RECLAIM_EXPLICIT) == AI_LIFETIME_OK,
          "shared reclaim after references converge");

    /* External tensors never allocate private storage and resist automatic reclaim. */
    nx_memory *external_memory = nx_memory_create(
        "external-memory", 256, 64, NX_MEMORY_FLAG_NONE
    );
    nx_handle_t external_memory_handle = external_memory->object.handle;
    ai_tensor *external = make_tensor(
        "external", AI_TENSOR_LIFETIME_EXTERNAL, AI_TENSOR_EXTERNAL
    );
    check(ai_tensor_allocate_backing(external, 64) ==
              AI_TENSOR_ERR_EXTERNAL_REQUIRES_EXISTING_BACKING,
          "external refuses private allocation");
    check(ai_tensor_bind_memory(external, external_memory_handle, 0) == AI_TENSOR_OK,
          "external binds imported storage");
    check(ai_tensor_lifetime_reclaim(external, AI_RECLAIM_MEMORY_PRESSURE) ==
              AI_LIFETIME_ERR_POLICY_DENIED,
          "external pressure reclaim denied");
    check(ai_tensor_lifetime_reclaim(external, AI_RECLAIM_EXPLICIT) == AI_LIFETIME_OK,
          "external explicit detach allowed");
    check(nx_memory_lookup(external_memory_handle) == external_memory,
          "external producer still owns imported memory");
    check(nx_memory_release(external_memory_handle),
          "release external memory creator reference");

    /* Pins override lifetime-class reclaim permission. */
    ai_tensor *pinned = make_tensor(
        "pinned", AI_TENSOR_LIFETIME_TEMPORARY, AI_TENSOR_PINNED
    );
    check(ai_tensor_allocate_backing(pinned, 64) == AI_TENSOR_OK,
          "pinned backing allocation");
    check(ai_tensor_lifetime_reclaim(pinned, AI_RECLAIM_FINAL_CONSUMER) ==
              AI_LIFETIME_ERR_PINNED,
          "pinned automatic reclaim denied");

    const ai_lifetime_stats *stats = ai_lifetime_get_stats();
    check(stats->live_by_class[AI_TENSOR_LIFETIME_TEMPORARY] == 2,
          "temporary live-class accounting");
    check(stats->live_by_class[AI_TENSOR_LIFETIME_PERSISTENT] == 1,
          "persistent live-class accounting");
    check(stats->live_by_class[AI_TENSOR_LIFETIME_CACHED] == 1,
          "cached live-class accounting");
    check(stats->live_by_class[AI_TENSOR_LIFETIME_SHARED] == 1,
          "shared live-class accounting");
    check(stats->live_by_class[AI_TENSOR_LIFETIME_EXTERNAL] == 1,
          "external live-class accounting");
    check(stats->reclaim_attempts == 10,
          "reclaim attempt accounting");
    check(stats->reclaim_successes == 6,
          "reclaim success accounting");
    check(stats->reclaim_denied == 4,
          "reclaim denial accounting");
    check(stats->binding_bytes_released == 6 * 256,
          "released binding byte accounting");
    check(stats->resident_bytes_reclaimed == 5 * NX_MEMORY_PAGE_SIZE,
          "actual resident reclaim accounting excludes externally-owned memory");

    check(ai_tensor_release(temporary), "release temporary tensor");
    check(ai_tensor_release(persistent), "release persistent tensor");
    check(ai_tensor_release(cached), "release cached tensor");
    check(ai_tensor_release(shared), "release shared tensor");
    check(ai_tensor_release(external), "release external tensor");
    check(ai_tensor_release(pinned), "release pinned tensor");

    stats = ai_lifetime_get_stats();
    for (u32 i = AI_TENSOR_LIFETIME_TEMPORARY; i < AI_TENSOR_LIFETIME_COUNT; ++i) {
        check(stats->live_by_class[i] == 0, "lifetime class drained after destruction");
    }

    puts("tensor lifetime tests: PASS");
    return 0;
}
