#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#include <ai/backing.h>
#include <ai/domain.h>
#include <ai/handle.h>
#include <ai/mapping.h>
#include <ai/tensor.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

#define TEST_ARENA_SIZE (4u * 1024u * 1024u)
#define REF_THREADS 8
#define REF_LOOPS 25000
#define RACE_ITERS 500

static u8 test_arena[TEST_ARENA_SIZE];
static usize test_offset = 0;

void early_heap_init(void) { test_offset = 0; }

void *kalloc(usize size, usize alignment) {
    if (alignment == 0) alignment = 1;
    usize aligned = (test_offset + alignment - 1) & ~(alignment - 1);
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

static ai_domain *make_domain(const char *name) {
    ai_domain_limits unlimited = {0, 0, 0};
    ai_domain *domain = ai_domain_create(name, 0, unlimited);
    assert(domain != NULL);
    assert(ai_domain_activate(domain));
    return domain;
}

static void destroy_empty_domain(ai_domain *domain) {
    assert(ai_handle_live_count(domain) == 0);
    assert(ai_mapping_live_count(domain) == 0);
    if (ai_domain_state_get(domain) == AI_DOMAIN_ACTIVE) {
        assert(ai_domain_begin_quiesce(domain));
    }
    assert(ai_domain_destroy(domain));
}

typedef struct {
    ai_tensor *tensor;
} ref_thread_arg;

static void *tensor_ref_worker(void *arg) {
    ref_thread_arg *ctx = (ref_thread_arg *)arg;
    for (int i = 0; i < REF_LOOPS; ++i) {
        assert(ai_tensor_get(ctx->tensor));
        assert(ai_tensor_put(ctx->tensor));
    }
    return NULL;
}

typedef struct {
    ai_domain *domain;
    ai_handle_t handle;
    atomic_int *successes;
} close_thread_arg;

static void *close_worker(void *arg) {
    close_thread_arg *ctx = (close_thread_arg *)arg;
    if (ai_handle_close(ctx->domain, ctx->handle)) {
        atomic_fetch_add_explicit(ctx->successes, 1, memory_order_relaxed);
    }
    return NULL;
}

typedef struct {
    ai_domain *source;
    ai_domain *target;
    ai_handle_t source_handle;
    atomic_int *start;
    ai_handle_t result;
} transfer_race_arg;

typedef struct {
    ai_domain *source;
    ai_handle_t source_handle;
    atomic_int *start;
    bool result;
} revoke_race_arg;

static void wait_for_start(atomic_int *start) {
    while (atomic_load_explicit(start, memory_order_acquire) == 0) {
        __asm__ volatile ("pause" ::: "memory");
    }
}

static void *transfer_worker(void *arg) {
    transfer_race_arg *ctx = (transfer_race_arg *)arg;
    wait_for_start(ctx->start);
    ctx->result = ai_handle_transfer(
        ctx->source,
        ctx->source_handle,
        ctx->target,
        AI_HANDLE_RIGHT_READ);
    return NULL;
}

static void *revoke_worker(void *arg) {
    revoke_race_arg *ctx = (revoke_race_arg *)arg;
    wait_for_start(ctx->start);
    ctx->result = ai_handle_revoke(ctx->source, ctx->source_handle);
    return NULL;
}

static void test_atomic_refcount_stress(void) {
    u64 shape[1] = {1024};
    ai_tensor *tensor = ai_tensor_create(
        "atomic-refcount",
        AI_DTYPE_F32,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL);
    assert(tensor != NULL);
    assert(ai_tensor_refcount(tensor) == 1);

    pthread_t threads[REF_THREADS];
    ref_thread_arg arg = {.tensor = tensor};
    for (int i = 0; i < REF_THREADS; ++i) {
        assert(pthread_create(&threads[i], NULL, tensor_ref_worker, &arg) == 0);
    }
    for (int i = 0; i < REF_THREADS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
    }

    assert(ai_tensor_refcount(tensor) == 1);
    assert(ai_tensor_put(tensor));
    assert(!ai_tensor_is_live(tensor));
    assert(ai_tensor_refcount(tensor) == 0);
    assert(!ai_tensor_put(tensor));
}

static void test_concurrent_close_exactly_once(ai_domain *domain) {
    u64 object = 0x11223344ull;
    ai_handle_t handle = ai_handle_install(
        domain,
        &object,
        AI_HANDLE_OBJECT_GENERIC,
        AI_HANDLE_RIGHT_READ);
    assert(handle != AI_HANDLE_INVALID);

    atomic_int successes;
    atomic_init(&successes, 0);
    pthread_t threads[REF_THREADS];
    close_thread_arg arg = {
        .domain = domain,
        .handle = handle,
        .successes = &successes
    };

    for (int i = 0; i < REF_THREADS; ++i) {
        assert(pthread_create(&threads[i], NULL, close_worker, &arg) == 0);
    }
    for (int i = 0; i < REF_THREADS; ++i) {
        assert(pthread_join(threads[i], NULL) == 0);
    }

    assert(atomic_load_explicit(&successes, memory_order_relaxed) == 1);
    assert(!ai_handle_is_valid(
        domain, handle, AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ));
}

static void test_revocation_pin_and_generation(ai_domain *domain) {
    u64 shape[1] = {1024};
    ai_tensor *tensor = ai_tensor_create(
        "revocation-pin",
        AI_DTYPE_F32,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL);
    ai_tensor_backing *backing = ai_backing_create_ram(
        tensor->bytes, AI_BACKING_ZEROED);
    assert(tensor && backing);
    assert(ai_tensor_attach_backing(tensor, backing, 0));

    ai_handle_t handle = ai_handle_install(
        domain,
        tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP);
    assert(handle != AI_HANDLE_INVALID);

    assert(ai_tensor_put(tensor));       /* creator ref */
    assert(ai_backing_put(backing));     /* creator ref */

    ai_tensor *pin = (ai_tensor *)ai_handle_acquire(
        domain,
        handle,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ);
    assert(pin == tensor);

    assert(ai_handle_revoke(domain, handle));
    assert(ai_handle_is_revoked(domain, handle));
    assert(ai_handle_revoked_count(domain) == 1);
    assert(ai_handle_resolve(
        domain, handle, AI_HANDLE_OBJECT_TENSOR, AI_HANDLE_RIGHT_READ) == NULL);
    assert(ai_handle_acquire(
        domain, handle, AI_HANDLE_OBJECT_TENSOR, AI_HANDLE_RIGHT_READ) == NULL);

    assert(ai_handle_reap_revoked(domain) == 1);
    assert(ai_handle_live_count(domain) == 0);
    assert(ai_tensor_is_live(tensor));   /* acquired pin still owns it */
    assert(ai_backing_is_live(backing));

    assert(ai_handle_release_object(pin, AI_HANDLE_OBJECT_TENSOR));
    assert(!ai_tensor_is_live(tensor));
    assert(!ai_backing_is_live(backing));

    u64 generic_a = 1;
    u64 generic_b = 2;
    ai_handle_t old = ai_handle_install(
        domain, &generic_a, AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ);
    assert(old != AI_HANDLE_INVALID);
    u32 old_slot = ai_handle_slot(old);
    u32 old_generation = ai_handle_generation(old);
    assert(ai_handle_revoke(domain, old));
    assert(ai_handle_reap_revoked(domain) == 1);

    ai_handle_t fresh = ai_handle_install(
        domain, &generic_b, AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ);
    assert(fresh != AI_HANDLE_INVALID);
    assert(ai_handle_slot(fresh) == old_slot);
    assert(ai_handle_generation(fresh) != old_generation);
    assert(ai_handle_resolve(
        domain, old, AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ) == NULL);
    assert(ai_handle_resolve(
        domain, fresh, AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ) == &generic_b);
    assert(ai_handle_close(domain, fresh));
}

static void test_mapping_view_survives_unmap(ai_domain *domain) {
    u64 shape[1] = {2048};
    ai_tensor *tensor = ai_tensor_create(
        "mapping-view",
        AI_DTYPE_F32,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL);
    ai_tensor_backing *backing = ai_backing_create_ram(
        tensor->bytes, AI_BACKING_ZEROED);
    assert(tensor && backing && ai_tensor_attach_backing(tensor, backing, 0));

    ai_handle_t handle = ai_handle_install(
        domain,
        tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_WRITE | AI_HANDLE_RIGHT_MAP);
    assert(handle != AI_HANDLE_INVALID);

    ai_mapping_id_t mapping = ai_tensor_map(
        domain, handle, AI_MAP_PROT_READ | AI_MAP_PROT_WRITE);
    assert(mapping != AI_MAPPING_INVALID);

    assert(ai_tensor_put(tensor));
    assert(ai_backing_put(backing));
    assert(ai_handle_close(domain, handle));

    ai_mapping_view view = {0};
    assert(ai_mapping_acquire_view(domain, mapping, &view));
    assert(view.valid && view.tensor == tensor && view.backing == backing);

    ((volatile u8 *)view.kernel_base)[0] = 0x5a;
    assert(ai_tensor_unmap(domain, mapping));
    assert(ai_mapping_live_count(domain) == 0);
    assert(!ai_mapping_acquire_view(domain, mapping, &(ai_mapping_view){0}));
    assert(ai_tensor_is_live(tensor));
    assert(ai_backing_is_live(backing));
    assert(((volatile u8 *)view.kernel_base)[0] == 0x5a);

    ai_mapping_release_view(&view);
    assert(!ai_tensor_is_live(tensor));
    assert(!ai_backing_is_live(backing));
}

static void test_transfer_revoke_race(ai_domain *source, ai_domain *target) {
    static u64 objects[RACE_ITERS];

    for (int i = 0; i < RACE_ITERS; ++i) {
        objects[i] = (u64)i + 1ull;
        ai_handle_t source_handle = ai_handle_install(
            source,
            &objects[i],
            AI_HANDLE_OBJECT_GENERIC,
            AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_TRANSFER);
        assert(source_handle != AI_HANDLE_INVALID);

        atomic_int start;
        atomic_init(&start, 0);
        transfer_race_arg transfer = {
            .source = source,
            .target = target,
            .source_handle = source_handle,
            .start = &start,
            .result = AI_HANDLE_INVALID
        };
        revoke_race_arg revoke = {
            .source = source,
            .source_handle = source_handle,
            .start = &start,
            .result = false
        };

        pthread_t transfer_thread;
        pthread_t revoke_thread;
        assert(pthread_create(&transfer_thread, NULL, transfer_worker, &transfer) == 0);
        assert(pthread_create(&revoke_thread, NULL, revoke_worker, &revoke) == 0);
        atomic_store_explicit(&start, 1, memory_order_release);
        assert(pthread_join(transfer_thread, NULL) == 0);
        assert(pthread_join(revoke_thread, NULL) == 0);

        if (transfer.result != AI_HANDLE_INVALID) {
            assert(!revoke.result);
            assert(ai_handle_resolve(
                target,
                transfer.result,
                AI_HANDLE_OBJECT_GENERIC,
                AI_HANDLE_RIGHT_READ) == &objects[i]);
            assert(ai_handle_close(target, transfer.result));
        } else {
            assert(revoke.result);
            assert(ai_handle_is_revoked(source, source_handle));
            assert(ai_handle_reap_revoked(source) == 1);
        }

        assert(ai_handle_live_count(source) == 0);
        assert(ai_handle_live_count(target) == 0);
    }
}

int main(void) {
    early_heap_init();
    ai_backing_system_init();
    ai_tensor_system_init();
    ai_domain_system_init();

    ai_domain *a = make_domain("concurrency-a");
    ai_domain *b = make_domain("concurrency-b");

    test_atomic_refcount_stress();
    test_concurrent_close_exactly_once(a);
    test_revocation_pin_and_generation(a);
    test_mapping_view_survives_unmap(a);
    test_transfer_revoke_race(a, b);

    assert(ai_tensor_count() == 0);
    assert(ai_backing_count() == 0);
    destroy_empty_domain(a);
    destroy_empty_domain(b);
    assert(ai_domain_count() == 0);
    return 0;
}
