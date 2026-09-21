#include <assert.h>

#include <ai/domain.h>
#include <ai/handle.h>
#include <ai/tensor.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

#define TEST_ARENA_SIZE (512u * 1024u)

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

static ai_tensor make_tensor(void) {
    ai_tensor tensor = {0};
    tensor.id = 77;
    tensor.name = "delegation-test";
    tensor.dtype = AI_DTYPE_F32;
    tensor.ndim = 2;
    tensor.shape[0] = 4;
    tensor.shape[1] = 4;
    tensor.bytes = 64;
    tensor.location = AI_LOC_CPU_RAM;
    tensor.flags = AI_TENSOR_EPHEMERAL;
    return tensor;
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
    if (domain->state == AI_DOMAIN_ACTIVE) {
        assert(ai_domain_begin_quiesce(domain));
    }
    assert(ai_domain_destroy(domain));
}

int main(void) {
    early_heap_init();
    ai_domain_system_init();

    ai_domain *a = make_domain("producer");
    ai_domain *b = make_domain("consumer-b");
    ai_domain *c = make_domain("consumer-c");
    ai_domain *full = make_domain("full-target");
    ai_domain *quiescing_target = make_domain("quiescing-target");
    ai_domain *quiescing_source = make_domain("quiescing-source");

    ai_tensor tensor = make_tensor();

    ai_handle_rights_t source_rights =
        AI_HANDLE_RIGHT_READ |
        AI_HANDLE_RIGHT_WRITE |
        AI_HANDLE_RIGHT_MAP |
        AI_HANDLE_RIGHT_SHARE |
        AI_HANDLE_RIGHT_TRANSFER;

    ai_handle_t source = ai_handle_install(
        a,
        &tensor,
        AI_HANDLE_OBJECT_TENSOR,
        source_rights
    );
    assert(source != AI_HANDLE_INVALID);
    assert(ai_handle_live_count(a) == 1);

    /* SHARE creates a recipient-local token and preserves the source. */
    ai_handle_t b_shared = ai_handle_share(
        a,
        source,
        b,
        AI_HANDLE_RIGHT_READ |
        AI_HANDLE_RIGHT_MAP |
        AI_HANDLE_RIGHT_SHARE
    );
    assert(b_shared != AI_HANDLE_INVALID);
    assert(ai_handle_domain_tag(b_shared) == (u32)b->id);
    assert(ai_handle_resolve(
               b,
               b_shared,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP) == &tensor);
    assert(ai_handle_resolve(
               b,
               b_shared,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_WRITE) == NULL);
    assert(ai_handle_resolve(
               a,
               source,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_WRITE) == &tensor);
    assert(ai_handle_live_count(a) == 1);
    assert(ai_handle_live_count(b) == 1);

    /* Copying A's numeric token into B remains useless. */
    assert(ai_handle_resolve(
               b,
               source,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_READ) == NULL);

    /* Delegated SHARE may itself be delegated, but only with a subset. */
    ai_handle_t c_shared = ai_handle_share(
        b,
        b_shared,
        c,
        AI_HANDLE_RIGHT_READ
    );
    assert(c_shared != AI_HANDLE_INVALID);
    assert(ai_handle_resolve(
               c,
               c_shared,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_READ) == &tensor);
    assert(ai_handle_get_rights(c, c_shared) == AI_HANDLE_RIGHT_READ);
    assert(ai_handle_share(
               c,
               c_shared,
               a,
               AI_HANDLE_RIGHT_READ) == AI_HANDLE_INVALID);

    /* No escalation, no zero-right delegation, and no same-domain cloning. */
    assert(ai_handle_share(
               a,
               source,
               b,
               AI_HANDLE_RIGHT_ADMIN) == AI_HANDLE_INVALID);
    assert(ai_handle_share(a, source, b, 0) == AI_HANDLE_INVALID);
    assert(ai_handle_share(
               a,
               source,
               a,
               AI_HANDLE_RIGHT_READ) == AI_HANDLE_INVALID);

    ai_handle_t no_share = ai_handle_install(
        a,
        &tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP
    );
    assert(no_share != AI_HANDLE_INVALID);
    assert(ai_handle_share(
               a,
               no_share,
               b,
               AI_HANDLE_RIGHT_READ) == AI_HANDLE_INVALID);
    assert(ai_handle_close(a, no_share));

    /* TRANSFER commits destination first and invalidates the source on success. */
    ai_handle_t movable = ai_handle_install(
        a,
        &tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ |
        AI_HANDLE_RIGHT_WRITE |
        AI_HANDLE_RIGHT_TRANSFER
    );
    assert(movable != AI_HANDLE_INVALID);
    u32 a_before_transfer = ai_handle_live_count(a);
    u32 c_before_transfer = ai_handle_live_count(c);

    ai_handle_t c_moved = ai_handle_transfer(
        a,
        movable,
        c,
        AI_HANDLE_RIGHT_READ
    );
    assert(c_moved != AI_HANDLE_INVALID);
    assert(ai_handle_resolve(
               a,
               movable,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_READ) == NULL);
    assert(ai_handle_resolve(
               c,
               c_moved,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_READ) == &tensor);
    assert(ai_handle_resolve(
               c,
               c_moved,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_WRITE) == NULL);
    assert(ai_handle_live_count(a) == a_before_transfer - 1u);
    assert(ai_handle_live_count(c) == c_before_transfer + 1u);

    /* Missing TRANSFER authority leaves the source untouched. */
    ai_handle_t immovable = ai_handle_install(
        a,
        &tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ
    );
    assert(immovable != AI_HANDLE_INVALID);
    assert(ai_handle_transfer(
               a,
               immovable,
               c,
               AI_HANDLE_RIGHT_READ) == AI_HANDLE_INVALID);
    assert(ai_handle_resolve(
               a,
               immovable,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_READ) == &tensor);

    /* Delegation endpoints must both be ACTIVE. */
    assert(ai_domain_begin_quiesce(quiescing_target));
    assert(ai_handle_share(
               a,
               source,
               quiescing_target,
               AI_HANDLE_RIGHT_READ) == AI_HANDLE_INVALID);

    ai_handle_t qt_transfer_source = ai_handle_install(
        a,
        &tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_TRANSFER
    );
    assert(qt_transfer_source != AI_HANDLE_INVALID);
    assert(ai_handle_transfer(
               a,
               qt_transfer_source,
               quiescing_target,
               AI_HANDLE_RIGHT_READ) == AI_HANDLE_INVALID);
    assert(ai_handle_resolve(
               a,
               qt_transfer_source,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_READ) == &tensor);

    ai_handle_t qs_source = ai_handle_install(
        quiescing_source,
        &tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ |
        AI_HANDLE_RIGHT_SHARE |
        AI_HANDLE_RIGHT_TRANSFER
    );
    assert(qs_source != AI_HANDLE_INVALID);
    assert(ai_domain_begin_quiesce(quiescing_source));
    assert(ai_handle_share(
               quiescing_source,
               qs_source,
               b,
               AI_HANDLE_RIGHT_READ) == AI_HANDLE_INVALID);
    assert(ai_handle_transfer(
               quiescing_source,
               qs_source,
               b,
               AI_HANDLE_RIGHT_READ) == AI_HANDLE_INVALID);
    assert(ai_handle_resolve(
               quiescing_source,
               qs_source,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_READ) == &tensor);

    /* A full destination must not destroy the source during failed transfer. */
    u64 objects[AI_DOMAIN_MAX_HANDLES];
    ai_handle_t filler[AI_DOMAIN_MAX_HANDLES];
    for (u32 i = 0; i < AI_DOMAIN_MAX_HANDLES; ++i) {
        objects[i] = i + 1u;
        filler[i] = ai_handle_install(
            full,
            &objects[i],
            AI_HANDLE_OBJECT_GENERIC,
            AI_HANDLE_RIGHT_READ
        );
        assert(filler[i] != AI_HANDLE_INVALID);
    }
    assert(ai_handle_live_count(full) == AI_DOMAIN_MAX_HANDLES);

    ai_handle_t rollback_source = ai_handle_install(
        a,
        &tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_TRANSFER
    );
    assert(rollback_source != AI_HANDLE_INVALID);
    u32 a_before_failed_move = ai_handle_live_count(a);
    assert(ai_handle_transfer(
               a,
               rollback_source,
               full,
               AI_HANDLE_RIGHT_READ) == AI_HANDLE_INVALID);
    assert(ai_handle_live_count(a) == a_before_failed_move);
    assert(ai_handle_resolve(
               a,
               rollback_source,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_READ) == &tensor);

    /* Closing a recipient SHARE handle never invalidates the source. */
    assert(ai_handle_close(b, b_shared));
    assert(ai_handle_resolve(
               a,
               source,
               AI_HANDLE_OBJECT_TENSOR,
               AI_HANDLE_RIGHT_READ) == &tensor);

    /* Cleanup. */
    assert(ai_handle_close(c, c_shared));
    assert(ai_handle_close(c, c_moved));
    assert(ai_handle_close(a, immovable));
    assert(ai_handle_close(a, qt_transfer_source));
    assert(ai_handle_close(a, rollback_source));
    assert(ai_handle_close(a, source));
    assert(ai_handle_close(quiescing_source, qs_source));

    for (u32 i = 0; i < AI_DOMAIN_MAX_HANDLES; ++i) {
        assert(ai_handle_close(full, filler[i]));
    }

    destroy_empty_domain(a);
    destroy_empty_domain(b);
    destroy_empty_domain(c);
    destroy_empty_domain(full);
    destroy_empty_domain(quiescing_target);
    destroy_empty_domain(quiescing_source);
    assert(ai_domain_count() == 0);

    return 0;
}
