#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <ai/backing.h>
#include <ai/domain.h>
#include <ai/handle.h>
#include <ai/mapping.h>
#include <ai/tensor.h>
#include <kernel/atomic.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

#define TEST_ARENA_SIZE (32u * 1024u * 1024u)
#define TOKEN_FUZZ_ITERS 100000u
#define MAPPING_FUZZ_ITERS 50000u
#define GEN_MASK 0x00ffffffu

static u8 test_arena[TEST_ARENA_SIZE];
static usize test_offset = 0;

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

static uint64_t xorshift64(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static ai_domain *make_domain(const char *name) {
    ai_domain_limits unlimited = {0, 0, 0};
    ai_domain *domain = ai_domain_create(name, 0, unlimited);
    assert(domain != NULL);
    assert(ai_domain_activate(domain));
    return domain;
}

static void destroy_empty_domain(ai_domain *domain) {
    assert(domain != NULL);
    assert(ai_handle_live_count(domain) == 0);
    assert(ai_mapping_live_count(domain) == 0);
    if (ai_domain_state_get(domain) == AI_DOMAIN_ACTIVE) {
        assert(ai_domain_begin_quiesce(domain));
    }
    assert(ai_domain_destroy(domain));
}

static ai_tensor *make_page_tensor(ai_tensor_backing **backing_out) {
    u64 shape[1] = {1024}; /* 4096 bytes f32 */
    ai_tensor *tensor = ai_tensor_create(
        "security-page",
        AI_DTYPE_F32,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL);
    assert(tensor != NULL);
    assert(tensor->bytes == 4096);

    ai_tensor_backing *backing = ai_backing_create_ram(
        tensor->bytes, AI_BACKING_ZEROED);
    assert(backing != NULL);
    assert(ai_tensor_attach_backing(tensor, backing, 0));
    if (backing_out) *backing_out = backing;
    return tensor;
}

static void test_rights_matrix_and_type_checks(void) {
    ai_domain *domain = make_domain("rights-matrix");
    u64 object = 0x1122334455667788ull;

    assert(!ai_handle_rights_valid(0));
    assert(!ai_handle_rights_valid(1u << 6));
    for (u32 rights = 1; rights <= AI_HANDLE_RIGHT_ALL; ++rights) {
        assert(ai_handle_rights_valid(rights));
    }

    ai_handle_t handle = ai_handle_install(
        domain, &object, AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_ALL);
    assert(handle != AI_HANDLE_INVALID);
    assert(ai_handle_resolve(domain, handle, AI_HANDLE_OBJECT_GENERIC, 0) == &object);
    assert(ai_handle_resolve(domain, handle, AI_HANDLE_OBJECT_TENSOR, 0) == NULL);

    for (u32 required = 1; required <= AI_HANDLE_RIGHT_ALL; ++required) {
        assert(ai_handle_has_rights(domain, handle, required));
        assert(ai_handle_resolve(
            domain, handle, AI_HANDLE_OBJECT_GENERIC, required) == &object);
    }

    const ai_handle_rights_t attenuated =
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP | AI_HANDLE_RIGHT_SHARE;
    assert(ai_handle_restrict_rights(domain, handle, attenuated));
    assert(!ai_handle_restrict_rights(
        domain, handle, attenuated | AI_HANDLE_RIGHT_WRITE));
    assert(!ai_handle_restrict_rights(domain, handle, 0));

    for (u32 required = 1; required <= AI_HANDLE_RIGHT_ALL; ++required) {
        bool expected = (required & attenuated) == required;
        assert(ai_handle_has_rights(domain, handle, required) == expected);
        assert((ai_handle_resolve(
            domain, handle, AI_HANDLE_OBJECT_GENERIC, required) != NULL) == expected);
    }

    assert(ai_handle_install(
        domain, &object, AI_HANDLE_OBJECT_GENERIC, 1u << 6) == AI_HANDLE_INVALID);
    assert(ai_handle_install(
        domain, &object, AI_HANDLE_OBJECT_NONE, AI_HANDLE_RIGHT_READ) == AI_HANDLE_INVALID);
    assert(ai_handle_install(
        domain, &object, AI_HANDLE_OBJECT_ANY, AI_HANDLE_RIGHT_READ) == AI_HANDLE_INVALID);

    assert(ai_handle_close(domain, handle));
    destroy_empty_domain(domain);
}

static void test_handle_token_fuzz(void) {
    ai_domain *a = make_domain("token-fuzz-a");
    ai_domain *b = make_domain("token-fuzz-b");
    u64 object = 0xfeedfacecafebeefull;
    ai_handle_t handle = ai_handle_install(
        a, &object, AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ);
    assert(handle != AI_HANDLE_INVALID);

    assert(ai_handle_resolve(
        b, handle, AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ) == NULL);
    assert(ai_handle_resolve(
        a, 0, AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ) == NULL);
    assert(ai_handle_resolve(
        a, ~0ull, AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ) == NULL);

    for (u32 bit = 0; bit < 64; ++bit) {
        ai_handle_t forged = handle ^ (1ull << bit);
        if (forged == handle) continue;
        assert(ai_handle_resolve(
            a, forged, AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ) == NULL);
        assert(!ai_handle_close(a, forged));
    }

    uint64_t rng = 0x9e3779b97f4a7c15ull;
    for (u32 i = 0; i < TOKEN_FUZZ_ITERS; ++i) {
        ai_handle_t forged = xorshift64(&rng);
        if (forged == handle) continue;
        assert(ai_handle_resolve(
            a, forged, AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ) == NULL);
        assert(!ai_handle_is_valid(
            a, forged, AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ));
    }

    assert(ai_handle_resolve(
        a, handle, AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ) == &object);
    assert(ai_handle_close(a, handle));
    destroy_empty_domain(a);
    destroy_empty_domain(b);
}

static void test_handle_table_exhaustion_and_reuse(void) {
    ai_domain *domain = make_domain("handle-exhaustion");
    u64 objects[AI_DOMAIN_MAX_HANDLES + 1u];
    ai_handle_t handles[AI_DOMAIN_MAX_HANDLES];
    ai_handle_t replacement[AI_DOMAIN_MAX_HANDLES / 2u];

    for (u32 i = 0; i < AI_DOMAIN_MAX_HANDLES; ++i) {
        objects[i] = i + 1u;
        handles[i] = ai_handle_install(
            domain, &objects[i], AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ);
        assert(handles[i] != AI_HANDLE_INVALID);
    }
    assert(ai_handle_live_count(domain) == AI_DOMAIN_MAX_HANDLES);
    objects[AI_DOMAIN_MAX_HANDLES] = 9999;
    assert(ai_handle_install(
        domain,
        &objects[AI_DOMAIN_MAX_HANDLES],
        AI_HANDLE_OBJECT_GENERIC,
        AI_HANDLE_RIGHT_READ) == AI_HANDLE_INVALID);

    for (u32 i = 0; i < AI_DOMAIN_MAX_HANDLES; i += 2u) {
        assert(ai_handle_revoke(domain, handles[i]));
    }
    assert(ai_handle_revoked_count(domain) == AI_DOMAIN_MAX_HANDLES / 2u);
    assert(ai_handle_live_count(domain) == AI_DOMAIN_MAX_HANDLES);
    assert(ai_handle_install(
        domain,
        &objects[AI_DOMAIN_MAX_HANDLES],
        AI_HANDLE_OBJECT_GENERIC,
        AI_HANDLE_RIGHT_READ) == AI_HANDLE_INVALID);

    assert(ai_handle_reap_revoked(domain) == AI_DOMAIN_MAX_HANDLES / 2u);
    assert(ai_handle_revoked_count(domain) == 0);
    assert(ai_handle_live_count(domain) == AI_DOMAIN_MAX_HANDLES / 2u);

    for (u32 i = 0; i < AI_DOMAIN_MAX_HANDLES / 2u; ++i) {
        replacement[i] = ai_handle_install(
            domain,
            &objects[i],
            AI_HANDLE_OBJECT_GENERIC,
            AI_HANDLE_RIGHT_READ);
        assert(replacement[i] != AI_HANDLE_INVALID);
    }
    assert(ai_handle_live_count(domain) == AI_DOMAIN_MAX_HANDLES);

    for (u32 i = 0; i < AI_DOMAIN_MAX_HANDLES; i += 2u) {
        assert(ai_handle_resolve(
            domain, handles[i], AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ) == NULL);
    }

    for (u32 i = 1; i < AI_DOMAIN_MAX_HANDLES; i += 2u) {
        assert(ai_handle_close(domain, handles[i]));
    }
    for (u32 i = 0; i < AI_DOMAIN_MAX_HANDLES / 2u; ++i) {
        assert(ai_handle_close(domain, replacement[i]));
    }
    destroy_empty_domain(domain);
}

static void test_generation_wrap_is_terminal(void) {
    ai_domain *domain = make_domain("generation-terminal");
    u64 a = 1, b = 2;
    ai_handle_t original = ai_handle_install(
        domain, &a, AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ);
    assert(original != AI_HANDLE_INVALID);
    u32 slot = ai_handle_slot(original);
    assert(slot < AI_DOMAIN_MAX_HANDLES);

    /* White-box boundary injection: emulate the final representable generation. */
    domain->handles.entries[slot].generation = GEN_MASK;
    ai_handle_t final_generation =
        ((u64)(u32)domain->id << 32) | ((u64)GEN_MASK << 8) | (u64)(slot + 1u);
    assert(ai_handle_close(domain, final_generation));
    assert(domain->handles.entries[slot].generation == 0u);

    ai_handle_t fresh = ai_handle_install(
        domain, &b, AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ);
    assert(fresh != AI_HANDLE_INVALID);
    assert(ai_handle_slot(fresh) != slot);
    assert(ai_handle_resolve(
        domain, final_generation, AI_HANDLE_OBJECT_GENERIC, AI_HANDLE_RIGHT_READ) == NULL);
    assert(ai_handle_close(domain, fresh));
    destroy_empty_domain(domain);
}

static void test_mapping_security_exhaustion_and_fuzz(void) {
    ai_domain *a = make_domain("mapping-a");
    ai_domain *b = make_domain("mapping-b");
    ai_tensor_backing *backing = NULL;
    ai_tensor *tensor = make_page_tensor(&backing);

    ai_handle_t handle = ai_handle_install(
        a,
        tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_WRITE | AI_HANDLE_RIGHT_MAP);
    assert(handle != AI_HANDLE_INVALID);

    assert(ai_tensor_map(a, handle, 0) == AI_MAPPING_INVALID);
    assert(ai_tensor_map(a, handle, AI_MAP_PROT_WRITE) == AI_MAPPING_INVALID);
    assert(ai_tensor_map(a, handle, AI_MAP_PROT_READ | (1u << 7)) == AI_MAPPING_INVALID);
    assert(ai_tensor_map_range(a, handle, 1, 64, AI_MAP_PROT_READ) == AI_MAPPING_INVALID);
    assert(ai_tensor_map_range(a, handle, 0, tensor->bytes + 1, AI_MAP_PROT_READ) == AI_MAPPING_INVALID);
    assert(ai_tensor_map_range(a, handle, tensor->bytes, 1, AI_MAP_PROT_READ) == AI_MAPPING_INVALID);

    ai_mapping_id_t mapping = ai_tensor_map(
        a, handle, AI_MAP_PROT_READ | AI_MAP_PROT_WRITE);
    assert(mapping != AI_MAPPING_INVALID);
    assert(ai_mapping_resolve(b, mapping) == NULL);

    for (u32 bit = 0; bit < 64; ++bit) {
        ai_mapping_id_t forged = mapping ^ (1ull << bit);
        if (forged == mapping) continue;
        ai_mapping_view view = {0};
        assert(!ai_mapping_acquire_view(a, forged, &view));
        assert(!ai_tensor_unmap(a, forged));
    }

    uint64_t rng = 0xd1b54a32d192ed03ull;
    for (u32 i = 0; i < MAPPING_FUZZ_ITERS; ++i) {
        ai_mapping_id_t forged = xorshift64(&rng);
        if (forged == mapping) continue;
        ai_mapping_view view = {0};
        u64 physical = 0;
        assert(!ai_mapping_acquire_view(a, forged, &view));
        assert(!ai_mapping_translate(a, forged, 0, &physical));
    }
    assert(ai_mapping_resolve(a, mapping) != NULL);
    assert(ai_tensor_unmap(a, mapping));

    ai_mapping_id_t maps[AI_DOMAIN_MAX_MAPPINGS];
    for (u32 i = 0; i < AI_DOMAIN_MAX_MAPPINGS; ++i) {
        maps[i] = ai_tensor_map(a, handle, AI_MAP_PROT_READ);
        assert(maps[i] != AI_MAPPING_INVALID);
    }
    assert(ai_mapping_live_count(a) == AI_DOMAIN_MAX_MAPPINGS);
    assert(ai_backing_mapping_count(backing) == AI_DOMAIN_MAX_MAPPINGS);
    assert(ai_tensor_map(a, handle, AI_MAP_PROT_READ) == AI_MAPPING_INVALID);

    for (u32 i = 0; i < AI_DOMAIN_MAX_MAPPINGS; i += 2u) {
        assert(ai_tensor_unmap(a, maps[i]));
    }
    assert(ai_mapping_live_count(a) == AI_DOMAIN_MAX_MAPPINGS / 2u);

    ai_mapping_id_t replacement[AI_DOMAIN_MAX_MAPPINGS / 2u];
    for (u32 i = 0; i < AI_DOMAIN_MAX_MAPPINGS / 2u; ++i) {
        replacement[i] = ai_tensor_map(a, handle, AI_MAP_PROT_READ);
        assert(replacement[i] != AI_MAPPING_INVALID);
    }
    for (u32 i = 0; i < AI_DOMAIN_MAX_MAPPINGS; i += 2u) {
        assert(ai_mapping_resolve(a, maps[i]) == NULL);
    }

    for (u32 i = 1; i < AI_DOMAIN_MAX_MAPPINGS; i += 2u) {
        assert(ai_tensor_unmap(a, maps[i]));
    }
    for (u32 i = 0; i < AI_DOMAIN_MAX_MAPPINGS / 2u; ++i) {
        assert(ai_tensor_unmap(a, replacement[i]));
    }
    assert(ai_backing_mapping_count(backing) == 0);

    assert(ai_handle_close(a, handle));
    assert(ai_tensor_put(tensor));
    assert(ai_backing_put(backing));
    destroy_empty_domain(a);
    destroy_empty_domain(b);
}

static void test_mapping_generation_and_va_overflow_guards(void) {
    ai_mapping_table impossible;
    ai_mapping_table_init(&impossible, 0xffffffffull);
    assert(impossible.next_virtual_address == 0);

    ai_domain *domain = make_domain("mapping-generation-terminal");
    ai_tensor_backing *backing = NULL;
    ai_tensor *tensor = make_page_tensor(&backing);
    ai_handle_t handle = ai_handle_install(
        domain,
        tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP);
    assert(handle != AI_HANDLE_INVALID);

    ai_mapping_id_t original = ai_tensor_map(domain, handle, AI_MAP_PROT_READ);
    assert(original != AI_MAPPING_INVALID);
    u32 slot = ai_mapping_slot(original);
    domain->mappings.entries[slot].generation = GEN_MASK;
    ai_mapping_id_t final_generation =
        ((u64)(u32)domain->id << 32) | ((u64)GEN_MASK << 8) | (u64)(slot + 1u);
    assert(ai_tensor_unmap(domain, final_generation));
    assert(domain->mappings.entries[slot].generation == 0u);

    ai_mapping_id_t fresh = ai_tensor_map(domain, handle, AI_MAP_PROT_READ);
    assert(fresh != AI_MAPPING_INVALID);
    assert(ai_mapping_slot(fresh) != slot);
    assert(ai_mapping_resolve(domain, final_generation) == NULL);
    assert(ai_tensor_unmap(domain, fresh));

    /* Force the allocator to the last half-page of the domain VA window. */
    u64 window_start = AI_MAPPING_VA_BASE +
        ((domain->id - 1ull) * AI_MAPPING_DOMAIN_STRIDE);
    u64 window_end = window_start + AI_MAPPING_DOMAIN_STRIDE;
    domain->mappings.next_virtual_address = window_end - (AI_MAPPING_PAGE_SIZE / 2ull);
    u64 tensor_refs_before = ai_tensor_refcount(tensor);
    u64 backing_refs_before = ai_backing_refcount(backing);
    assert(ai_tensor_map(domain, handle, AI_MAP_PROT_READ) == AI_MAPPING_INVALID);
    assert(ai_tensor_refcount(tensor) == tensor_refs_before);
    assert(ai_backing_refcount(backing) == backing_refs_before);
    assert(ai_backing_mapping_count(backing) == 0);

    assert(ai_handle_close(domain, handle));
    assert(ai_tensor_put(tensor));
    assert(ai_backing_put(backing));
    destroy_empty_domain(domain);
}

static void test_refcount_and_accounting_boundaries(void) {
    u64 shape[1] = {1};
    ai_tensor *tensor = ai_tensor_create(
        "ref-boundary", AI_DTYPE_I8, 1, shape, AI_LOC_CPU_RAM, AI_TENSOR_EPHEMERAL);
    assert(tensor != NULL);

    ai_atomic_store_u64(&tensor->refcount, ~0ull);
    assert(!ai_tensor_get(tensor));
    ai_atomic_store_u64(&tensor->refcount, 0);
    assert(!ai_tensor_get(tensor));
    assert(!ai_tensor_put(tensor));
    ai_atomic_store_u64(&tensor->refcount, 1);
    assert(ai_tensor_put(tensor));
    assert(!ai_tensor_put(tensor));

    ai_tensor_backing *backing = ai_backing_create_ram(4096, 0);
    assert(backing != NULL);
    ai_atomic_store_u64(&backing->refcount, ~0ull);
    assert(!ai_backing_get(backing));
    ai_atomic_store_u64(&backing->refcount, 0);
    assert(!ai_backing_get(backing));
    assert(!ai_backing_put(backing));
    ai_atomic_store_u64(&backing->refcount, 1);

    ai_atomic_store_u64(&backing->mapping_count, ~0ull);
    assert(!ai_backing_mapping_acquire(backing));
    assert(ai_backing_refcount(backing) == 1);
    ai_atomic_store_u64(&backing->mapping_count, 0);
    assert(ai_backing_put(backing));
    assert(!ai_backing_put(backing));

    ai_domain *domain = make_domain("accounting-boundary");
    assert(ai_domain_charge_memory(domain, ~0ull));
    assert(!ai_domain_charge_memory(domain, 1));
    assert(domain->memory_used_bytes == ~0ull);
    ai_domain_uncharge_memory(domain, ~0ull);
    assert(domain->memory_used_bytes == 0);
    destroy_empty_domain(domain);
}

int main(void) {
    early_heap_init();
    ai_domain_system_init();
    ai_tensor_system_init();
    ai_backing_system_init();

    test_rights_matrix_and_type_checks();
    test_handle_token_fuzz();
    test_handle_table_exhaustion_and_reuse();
    test_generation_wrap_is_terminal();
    test_mapping_security_exhaustion_and_fuzz();
    test_mapping_generation_and_va_overflow_guards();
    test_refcount_and_accounting_boundaries();

    assert(ai_tensor_count() == 0);
    assert(ai_backing_count() == 0);
    assert(ai_domain_count() == 0);

    puts("Step 9 adversarial security/correctness tests passed.");
    return 0;
}
