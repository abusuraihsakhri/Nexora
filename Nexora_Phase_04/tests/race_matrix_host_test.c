#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#include <ai/backing.h>
#include <ai/domain.h>
#include <ai/handle.h>
#include <ai/mapping.h>
#include <ai/tensor.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

#define TEST_ARENA_SIZE (16u * 1024u * 1024u)
#define RACE_ITERS 750
#define CLOSE_THREADS 8

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

static void wait_start(atomic_int *start) {
    while (!atomic_load_explicit(start, memory_order_acquire)) {
        __asm__ volatile("pause" ::: "memory");
    }
}

typedef struct {
    ai_domain *domain;
    ai_handle_t handle;
    atomic_int *start;
    bool result;
} handle_race_arg;

static void *close_once(void *arg) {
    handle_race_arg *a = (handle_race_arg *)arg;
    wait_start(a->start);
    a->result = ai_handle_close(a->domain, a->handle);
    return NULL;
}

static void *revoke_once(void *arg) {
    handle_race_arg *a = (handle_race_arg *)arg;
    wait_start(a->start);
    a->result = ai_handle_revoke(a->domain, a->handle);
    return NULL;
}

typedef struct {
    ai_domain *domain;
    ai_handle_t handle;
    atomic_int *start;
    ai_tensor *pin;
} acquire_race_arg;

static void *acquire_once(void *arg) {
    acquire_race_arg *a = (acquire_race_arg *)arg;
    wait_start(a->start);
    a->pin = (ai_tensor *)ai_handle_acquire(
        a->domain, a->handle, AI_HANDLE_OBJECT_TENSOR, AI_HANDLE_RIGHT_READ);
    return NULL;
}

typedef struct {
    ai_domain *domain;
    ai_mapping_id_t mapping;
    atomic_int *start;
    bool result;
    ai_mapping_view view;
} mapping_race_arg;

static void *unmap_once(void *arg) {
    mapping_race_arg *a = (mapping_race_arg *)arg;
    wait_start(a->start);
    a->result = ai_tensor_unmap(a->domain, a->mapping);
    return NULL;
}

static void *view_once(void *arg) {
    mapping_race_arg *a = (mapping_race_arg *)arg;
    wait_start(a->start);
    a->result = ai_mapping_acquire_view(a->domain, a->mapping, &a->view);
    return NULL;
}

static void test_close_vs_revoke(ai_domain *domain) {
    static u64 objects[RACE_ITERS];
    for (int i = 0; i < RACE_ITERS; ++i) {
        objects[i] = (u64)i;
        ai_handle_t h = ai_handle_install(
            domain, &objects[i], AI_HANDLE_OBJECT_GENERIC,
            AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_SHARE);
        assert(h != AI_HANDLE_INVALID);

        atomic_int start;
        atomic_init(&start, 0);
        handle_race_arg close_arg = {domain, h, &start, false};
        handle_race_arg revoke_arg = {domain, h, &start, false};
        pthread_t tc, tr;
        assert(pthread_create(&tc, NULL, close_once, &close_arg) == 0);
        assert(pthread_create(&tr, NULL, revoke_once, &revoke_arg) == 0);
        atomic_store_explicit(&start, 1, memory_order_release);
        assert(pthread_join(tc, NULL) == 0);
        assert(pthread_join(tr, NULL) == 0);

        assert(close_arg.result); /* close wins or closes a just-revoked entry */
        assert(ai_handle_live_count(domain) == 0);
        assert(ai_handle_revoked_count(domain) == 0);
        assert(!ai_handle_is_valid(
            domain, h, AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ));
    }
}

static void test_close_vs_acquire(ai_domain *domain) {
    u64 shape[1] = {4};
    ai_tensor *tensor = ai_tensor_create(
        "close-acquire", AI_DTYPE_F32, 1, shape, AI_LOC_CPU_RAM, AI_TENSOR_EPHEMERAL);
    assert(tensor != NULL);

    for (int i = 0; i < RACE_ITERS; ++i) {
        ai_handle_t h = ai_handle_install(
            domain, tensor, AI_HANDLE_OBJECT_TENSOR, AI_HANDLE_RIGHT_READ);
        assert(h != AI_HANDLE_INVALID);
        u64 baseline = ai_tensor_refcount(tensor);
        assert(baseline == 2); /* creator + handle */

        atomic_int start;
        atomic_init(&start, 0);
        handle_race_arg close_arg = {domain, h, &start, false};
        acquire_race_arg acquire_arg = {domain, h, &start, NULL};
        pthread_t tc, ta;
        assert(pthread_create(&tc, NULL, close_once, &close_arg) == 0);
        assert(pthread_create(&ta, NULL, acquire_once, &acquire_arg) == 0);
        atomic_store_explicit(&start, 1, memory_order_release);
        assert(pthread_join(tc, NULL) == 0);
        assert(pthread_join(ta, NULL) == 0);

        assert(close_arg.result);
        if (acquire_arg.pin) {
            assert(acquire_arg.pin == tensor);
            assert(ai_tensor_is_live(tensor));
            assert(ai_handle_release_object(acquire_arg.pin, AI_HANDLE_OBJECT_TENSOR));
        }
        assert(ai_tensor_refcount(tensor) == 1);
    }

    assert(ai_tensor_put(tensor));
}

static void test_unmap_vs_view_and_multi_unmap(ai_domain *domain) {
    u64 shape[1] = {1024};
    ai_tensor *tensor = ai_tensor_create(
        "unmap-view", AI_DTYPE_F32, 1, shape, AI_LOC_CPU_RAM, AI_TENSOR_EPHEMERAL);
    ai_tensor_backing *backing = ai_backing_create_ram(tensor->bytes, AI_BACKING_ZEROED);
    assert(tensor && backing && ai_tensor_attach_backing(tensor, backing, 0));
    ai_handle_t h = ai_handle_install(
        domain, tensor, AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_WRITE | AI_HANDLE_RIGHT_MAP);
    assert(h != AI_HANDLE_INVALID);

    for (int i = 0; i < RACE_ITERS; ++i) {
        ai_mapping_id_t m = ai_tensor_map(domain, h, AI_MAP_PROT_READ);
        assert(m != AI_MAPPING_INVALID);
        atomic_int start;
        atomic_init(&start, 0);
        mapping_race_arg unmap_arg = {domain, m, &start, false, {0}};
        mapping_race_arg view_arg = {domain, m, &start, false, {0}};
        pthread_t tu, tv;
        assert(pthread_create(&tu, NULL, unmap_once, &unmap_arg) == 0);
        assert(pthread_create(&tv, NULL, view_once, &view_arg) == 0);
        atomic_store_explicit(&start, 1, memory_order_release);
        assert(pthread_join(tu, NULL) == 0);
        assert(pthread_join(tv, NULL) == 0);
        assert(unmap_arg.result);
        if (view_arg.result) {
            assert(view_arg.view.valid);
            assert(view_arg.view.tensor == tensor);
            assert(view_arg.view.backing == backing);
            ai_mapping_release_view(&view_arg.view);
        }
        assert(ai_mapping_live_count(domain) == 0);
        assert(ai_backing_mapping_count(backing) == 0);
    }

    for (int iter = 0; iter < RACE_ITERS / 3; ++iter) {
        ai_mapping_id_t m = ai_tensor_map(domain, h, AI_MAP_PROT_READ);
        assert(m != AI_MAPPING_INVALID);
        atomic_int start;
        atomic_init(&start, 0);
        pthread_t threads[CLOSE_THREADS];
        mapping_race_arg args[CLOSE_THREADS];
        for (int i = 0; i < CLOSE_THREADS; ++i) {
            args[i] = (mapping_race_arg){domain, m, &start, false, {0}};
            assert(pthread_create(&threads[i], NULL, unmap_once, &args[i]) == 0);
        }
        atomic_store_explicit(&start, 1, memory_order_release);
        int successes = 0;
        for (int i = 0; i < CLOSE_THREADS; ++i) {
            assert(pthread_join(threads[i], NULL) == 0);
            if (args[i].result) successes++;
        }
        assert(successes == 1);
        assert(ai_mapping_live_count(domain) == 0);
        assert(ai_backing_mapping_count(backing) == 0);
    }

    assert(ai_handle_close(domain, h));
    assert(ai_tensor_put(tensor));
    assert(ai_backing_put(backing));
}

int main(void) {
    early_heap_init();
    ai_domain_system_init();
    ai_tensor_system_init();
    ai_backing_system_init();

    ai_domain *domain = make_domain("race-matrix");
    test_close_vs_revoke(domain);
    test_close_vs_acquire(domain);
    test_unmap_vs_view_and_multi_unmap(domain);
    destroy_empty_domain(domain);

    assert(ai_tensor_count() == 0);
    assert(ai_backing_count() == 0);
    puts("Step 9 race matrix tests passed.");
    return 0;
}
