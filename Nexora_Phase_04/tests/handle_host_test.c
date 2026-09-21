#include <assert.h>

#include <ai/domain.h>
#include <ai/handle.h>
#include <ai/tensor.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

#define TEST_ARENA_SIZE (256u * 1024u)

static u8 test_arena[TEST_ARENA_SIZE];
static usize test_offset = 0;

void early_heap_init(void) {
    test_offset = 0;
}

void *kalloc(usize size, usize alignment) {
    if (alignment == 0) {
        alignment = 1;
    }

    usize aligned = (test_offset + alignment - 1) & ~(alignment - 1);
    if (aligned + size > TEST_ARENA_SIZE) {
        __builtin_trap();
    }

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

static ai_tensor make_tensor(u32 flags) {
    ai_tensor tensor = {0};
    tensor.id = 1;
    tensor.name = "host-test";
    tensor.dtype = AI_DTYPE_F32;
    tensor.ndim = 1;
    tensor.shape[0] = 16;
    tensor.bytes = 64;
    tensor.location = AI_LOC_CPU_RAM;
    tensor.flags = flags;
    return tensor;
}

int main(void) {
    early_heap_init();
    ai_domain_system_init();

    ai_domain_limits unlimited = {0, 0, 0};
    ai_domain *a = ai_domain_create("a", 0, unlimited);
    ai_domain *b = ai_domain_create("b", 0, unlimited);
    assert(a && b);
    assert(ai_domain_activate(a));
    assert(ai_domain_activate(b));

    ai_tensor writable = make_tensor(AI_TENSOR_EPHEMERAL);
    ai_tensor readonly = make_tensor(AI_TENSOR_PERSISTENT | AI_TENSOR_READONLY);

    assert(!ai_handle_rights_valid(0));
    assert(ai_handle_rights_valid(AI_HANDLE_RIGHT_READ));
    assert(ai_handle_rights_valid(AI_HANDLE_RIGHT_ALL));
    assert(!ai_handle_rights_valid(AI_HANDLE_RIGHT_ALL | (1u << 20)));

    assert(ai_handle_install(
               a,
               &writable,
               AI_HANDLE_OBJECT_TENSOR,
               0) == AI_HANDLE_INVALID);

    assert(ai_handle_install(
               a,
               &readonly,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_WRITE) ==
           AI_HANDLE_INVALID);

    ai_handle_t ro = ai_handle_install(
        a,
        &readonly,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP
    );
    assert(ro != AI_HANDLE_INVALID);
    assert(ai_handle_resolve(
               a,
               ro,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_READ) == &readonly);
    assert(ai_handle_resolve(
               a,
               ro,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_WRITE) == NULL);
    assert(ai_handle_close(a, ro));

    ai_handle_rights_t initial =
        AI_HANDLE_RIGHT_READ |
        AI_HANDLE_RIGHT_WRITE |
        AI_HANDLE_RIGHT_MAP |
        AI_HANDLE_RIGHT_SHARE |
        AI_HANDLE_RIGHT_TRANSFER;

    ai_handle_t h1 = ai_handle_install(
        a,
        &writable,
        AI_HANDLE_OBJECT_TENSOR,
        initial
    );
    assert(h1 != AI_HANDLE_INVALID);
    assert(ai_handle_domain_tag(h1) == (u32)a->id);
    assert(ai_handle_get_rights(a, h1) == initial);
    assert(ai_handle_get_rights(b, h1) == 0);

    assert(ai_handle_resolve(
               a,
               h1,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_READ) == &writable);
    assert(ai_handle_resolve(
               a,
               h1,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP) == &writable);
    assert(ai_handle_resolve(
               a,
               h1,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_ADMIN) == NULL);
    assert(ai_handle_resolve(
               b,
               h1,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_READ) == NULL);
    assert(ai_handle_resolve(
               a,
               h1,
               AI_HANDLE_OBJECT_WORK,
               AI_HANDLE_RIGHT_READ) == NULL);
    assert(ai_handle_resolve(
               a,
               h1,
               AI_HANDLE_OBJECT_TENSOR,
               (1u << 20)) == NULL);

    assert(ai_handle_has_rights(a, h1, AI_HANDLE_RIGHT_WRITE));
    assert(!ai_handle_has_rights(a, h1, AI_HANDLE_RIGHT_ADMIN));
    assert(!ai_handle_has_rights(b, h1, AI_HANDLE_RIGHT_READ));
    assert(!ai_handle_has_rights(a, h1, 0));

    ai_handle_rights_t reduced =
        AI_HANDLE_RIGHT_READ |
        AI_HANDLE_RIGHT_MAP |
        AI_HANDLE_RIGHT_SHARE;

    assert(ai_handle_restrict_rights(a, h1, reduced));
    assert(ai_handle_get_rights(a, h1) == reduced);
    assert(!ai_handle_has_rights(a, h1, AI_HANDLE_RIGHT_WRITE));
    assert(ai_handle_has_rights(
        a,
        h1,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP));
    assert(!ai_handle_restrict_rights(a, h1, initial));
    assert(!ai_handle_restrict_rights(a, h1, 0));

    assert(ai_handle_close(a, h1));
    assert(ai_handle_resolve(
               a,
               h1,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_READ) == NULL);
    assert(ai_handle_get_rights(a, h1) == 0);
    assert(!ai_handle_close(a, h1));

    ai_handle_t h2 = ai_handle_install(
        a,
        &writable,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ
    );
    assert(h2 != AI_HANDLE_INVALID);
    assert(h2 != h1);
    assert(ai_handle_slot(h2) == ai_handle_slot(h1));
    assert(ai_handle_generation(h2) != ai_handle_generation(h1));

    assert(ai_domain_begin_quiesce(a));
    assert(ai_handle_install(
               a,
               &writable,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_READ) == AI_HANDLE_INVALID);
    assert(ai_handle_resolve(
               a,
               h2,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_READ) == &writable);
    assert(ai_handle_restrict_rights(a, h2, AI_HANDLE_RIGHT_READ));
    assert(!ai_domain_destroy(a));
    assert(ai_handle_close(a, h2));
    assert(ai_domain_destroy(a));

    u64 objects[AI_DOMAIN_MAX_HANDLES + 1u];
    ai_handle_t handles[AI_DOMAIN_MAX_HANDLES];
    for (u32 i = 0; i < AI_DOMAIN_MAX_HANDLES; ++i) {
        objects[i] = i;
        handles[i] = ai_handle_install(
            b,
            &objects[i],
            AI_HANDLE_OBJECT_GENERIC,
            AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_ADMIN
        );
        assert(handles[i] != AI_HANDLE_INVALID);
    }

    objects[AI_DOMAIN_MAX_HANDLES] = 999;
    assert(ai_handle_install(
               b,
               &objects[AI_DOMAIN_MAX_HANDLES],
               AI_HANDLE_OBJECT_GENERIC,
               AI_HANDLE_RIGHT_READ) == AI_HANDLE_INVALID);
    assert(ai_handle_live_count(b) == AI_DOMAIN_MAX_HANDLES);

    for (u32 i = 0; i < AI_DOMAIN_MAX_HANDLES; ++i) {
        assert(ai_handle_close(b, handles[i]));
    }
    assert(ai_handle_live_count(b) == 0);
    assert(ai_domain_begin_quiesce(b));
    assert(ai_domain_destroy(b));
    assert(ai_domain_count() == 0);

    return 0;
}
