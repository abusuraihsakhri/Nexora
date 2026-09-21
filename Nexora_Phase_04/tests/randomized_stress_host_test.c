#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

#include <ai/backing.h>
#include <ai/domain.h>
#include <ai/handle.h>
#include <ai/mapping.h>
#include <ai/tensor.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

#define TEST_ARENA_SIZE (32u * 1024u * 1024u)
#define HANDLE_THREADS 8
#define HANDLE_ITERS 25000
#define MAPPING_THREADS 8
#define MAPPING_ITERS 4000
#define SHARE_THREADS 4
#define SHARE_ITERS 8000

static u8 test_arena[TEST_ARENA_SIZE];
static usize test_offset;

void early_heap_init(void) { test_offset = 0; }
void *kalloc(usize size, usize alignment) {
    if (alignment == 0) alignment = 1;
    usize aligned = (test_offset + alignment - 1u) & ~(alignment - 1u);
    if (aligned + size > TEST_ARENA_SIZE) __builtin_trap();
    void *p = &test_arena[aligned];
    test_offset = aligned + size;
    return p;
}
usize early_heap_used(void) { return test_offset; }
usize early_heap_capacity(void) { return TEST_ARENA_SIZE; }
__attribute__((noreturn)) void panic(const char *message) {
    (void)message;
    __builtin_trap();
}

static uint64_t rng_next(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static ai_domain *make_domain(const char *name) {
    ai_domain_limits unlimited = {0, 0, 0};
    ai_domain *d = ai_domain_create(name, 0, unlimited);
    assert(d && ai_domain_activate(d));
    return d;
}

static void destroy_empty_domain(ai_domain *d) {
    assert(ai_handle_live_count(d) == 0);
    assert(ai_mapping_live_count(d) == 0);
    if (ai_domain_state_get(d) == AI_DOMAIN_ACTIVE) assert(ai_domain_begin_quiesce(d));
    assert(ai_domain_destroy(d));
}

typedef struct {
    ai_domain *domain;
    u64 object;
    uint64_t seed;
} handle_worker_arg;

static void *handle_worker(void *arg) {
    handle_worker_arg *a = (handle_worker_arg *)arg;
    uint64_t rng = a->seed;
    for (int i = 0; i < HANDLE_ITERS; ++i) {
        ai_handle_rights_t rights = (ai_handle_rights_t)(rng_next(&rng) & AI_HANDLE_RIGHT_ALL);
        if (rights == 0) rights = AI_HANDLE_RIGHT_READ;
        ai_handle_t h = ai_handle_install(
            a->domain, &a->object, AI_HANDLE_OBJECT_GENERIC, rights);
        assert(h != AI_HANDLE_INVALID);
        assert(ai_handle_get_rights(a->domain, h) == rights);

        ai_handle_rights_t subset = rights;
        if ((rng_next(&rng) & 1ull) && (rights & (rights - 1u)) != 0) {
            u32 bit = (u32)(rng_next(&rng) % 6u);
            subset = rights & ~(1u << bit);
            if (subset == 0) subset = rights;
        }
        if (subset != rights) {
            assert(ai_handle_restrict_rights(a->domain, h, subset));
            assert(ai_handle_get_rights(a->domain, h) == subset);
        }

        if ((rng_next(&rng) % 4ull) == 0) {
            assert(ai_handle_revoke(a->domain, h));
            assert(!ai_handle_is_valid(a->domain, h, AI_HANDLE_OBJECT_GENERIC, 0));
        }
        assert(ai_handle_close(a->domain, h));
    }
    return NULL;
}

typedef struct {
    ai_domain *domain;
    ai_handle_t handle;
    uint64_t seed;
    u64 byte_offset;
} mapping_worker_arg;

static void *mapping_worker(void *arg) {
    mapping_worker_arg *a = (mapping_worker_arg *)arg;
    uint64_t rng = a->seed;
    for (int i = 0; i < MAPPING_ITERS; ++i) {
        ai_mapping_prot_t prot = (rng_next(&rng) & 1ull)
            ? AI_MAP_PROT_READ
            : (AI_MAP_PROT_READ | AI_MAP_PROT_WRITE);
        ai_mapping_id_t m = ai_tensor_map(a->domain, a->handle, prot);
        assert(m != AI_MAPPING_INVALID);

        ai_mapping_view view = {0};
        assert(ai_mapping_acquire_view(a->domain, m, &view));
        assert(view.valid);
        if ((prot & AI_MAP_PROT_WRITE) != 0 && (rng_next(&rng) & 3ull) == 0) {
            ((volatile u8 *)view.kernel_base)[a->byte_offset] = (u8)rng;
        } else {
            (void)((volatile u8 *)view.kernel_base)[a->byte_offset];
        }

        u64 physical = 0;
        assert(ai_mapping_translate(
            a->domain, m, view.virtual_base + a->byte_offset, &physical));
        assert(physical == view.physical_base + a->byte_offset);
        assert(ai_tensor_unmap(a->domain, m));
        ai_mapping_release_view(&view);
    }
    return NULL;
}

typedef struct {
    ai_domain *source;
    ai_domain *target;
    ai_handle_t source_handle;
    uint64_t seed;
} share_worker_arg;

static void *share_worker(void *arg) {
    share_worker_arg *a = (share_worker_arg *)arg;
    uint64_t rng = a->seed;
    for (int i = 0; i < SHARE_ITERS; ++i) {
        ai_handle_rights_t requested = (rng_next(&rng) & 1ull)
            ? AI_HANDLE_RIGHT_READ
            : (AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP);
        ai_handle_t h = ai_handle_share(
            a->source, a->source_handle, a->target, requested);
        assert(h != AI_HANDLE_INVALID);
        assert(ai_handle_get_rights(a->target, h) == requested);
        assert(ai_handle_close(a->target, h));
    }
    return NULL;
}

int main(void) {
    early_heap_init();
    ai_domain_system_init();
    ai_tensor_system_init();
    ai_backing_system_init();

    ai_domain *handle_domain = make_domain("random-handles");
    pthread_t handle_threads[HANDLE_THREADS];
    handle_worker_arg hargs[HANDLE_THREADS];
    for (int i = 0; i < HANDLE_THREADS; ++i) {
        hargs[i] = (handle_worker_arg){
            .domain = handle_domain,
            .object = (u64)i + 1ull,
            .seed = 0x9e3779b97f4a7c15ull ^ ((u64)i * 0x100000001b3ull)
        };
        assert(pthread_create(&handle_threads[i], NULL, handle_worker, &hargs[i]) == 0);
    }
    for (int i = 0; i < HANDLE_THREADS; ++i) assert(pthread_join(handle_threads[i], NULL) == 0);
    assert(ai_handle_live_count(handle_domain) == 0);
    assert(ai_handle_revoked_count(handle_domain) == 0);

    ai_domain *mapping_domain = make_domain("random-mappings");
    u64 shape[1] = {1024};
    ai_tensor *tensor = ai_tensor_create(
        "stress-tensor", AI_DTYPE_F32, 1, shape, AI_LOC_CPU_RAM, AI_TENSOR_EPHEMERAL);
    ai_tensor_backing *backing = ai_backing_create_ram(tensor->bytes, AI_BACKING_ZEROED);
    assert(tensor && backing && ai_tensor_attach_backing(tensor, backing, 0));
    ai_handle_t tensor_handle = ai_handle_install(
        mapping_domain, tensor, AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_WRITE | AI_HANDLE_RIGHT_MAP | AI_HANDLE_RIGHT_SHARE);
    assert(tensor_handle != AI_HANDLE_INVALID);

    pthread_t mapping_threads[MAPPING_THREADS];
    mapping_worker_arg margs[MAPPING_THREADS];
    for (int i = 0; i < MAPPING_THREADS; ++i) {
        margs[i] = (mapping_worker_arg){
            .domain = mapping_domain,
            .handle = tensor_handle,
            .seed = 0xd1b54a32d192ed03ull ^ ((u64)i * 0x9e3779b97f4a7c15ull),
            .byte_offset = (u64)i
        };
        assert(pthread_create(&mapping_threads[i], NULL, mapping_worker, &margs[i]) == 0);
    }
    for (int i = 0; i < MAPPING_THREADS; ++i) assert(pthread_join(mapping_threads[i], NULL) == 0);
    assert(ai_mapping_live_count(mapping_domain) == 0);
    assert(ai_backing_mapping_count(backing) == 0);

    ai_domain *targets[SHARE_THREADS];
    pthread_t share_threads[SHARE_THREADS];
    share_worker_arg sargs[SHARE_THREADS];
    for (int i = 0; i < SHARE_THREADS; ++i) {
        targets[i] = make_domain("share-target");
        sargs[i] = (share_worker_arg){
            .source = mapping_domain,
            .target = targets[i],
            .source_handle = tensor_handle,
            .seed = 0xa0761d6478bd642full ^ (u64)i
        };
        assert(pthread_create(&share_threads[i], NULL, share_worker, &sargs[i]) == 0);
    }
    for (int i = 0; i < SHARE_THREADS; ++i) {
        assert(pthread_join(share_threads[i], NULL) == 0);
        assert(ai_handle_live_count(targets[i]) == 0);
        destroy_empty_domain(targets[i]);
    }

    assert(ai_handle_close(mapping_domain, tensor_handle));
    assert(ai_tensor_put(tensor));
    assert(ai_backing_put(backing));
    destroy_empty_domain(mapping_domain);
    destroy_empty_domain(handle_domain);

    assert(ai_tensor_count() == 0);
    assert(ai_backing_count() == 0);
    assert(ai_domain_count() == 0);
    puts("Step 9 randomized concurrent stress tests passed.");
    return 0;
}
