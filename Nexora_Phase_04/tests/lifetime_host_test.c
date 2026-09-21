#include <assert.h>

#include <ai/backing.h>
#include <ai/domain.h>
#include <ai/handle.h>
#include <ai/mapping.h>
#include <ai/tensor.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

#define TEST_ARENA_SIZE (1024u * 1024u)

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
    assert(ai_domain_begin_quiesce(domain));
    assert(ai_domain_destroy(domain));
}

int main(void) {
    early_heap_init();
    ai_backing_system_init();
    ai_tensor_system_init();
    ai_domain_system_init();

    ai_domain *producer = make_domain("lifetime-producer");
    ai_domain *consumer = make_domain("lifetime-consumer");

    u64 shape[1] = {2048}; /* 8192 bytes */
    ai_tensor *tensor = ai_tensor_create(
        "lifetime-tensor",
        AI_DTYPE_F32,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL
    );
    assert(tensor != NULL);
    assert(ai_tensor_count() == 1);
    assert(ai_tensor_is_live(tensor));
    assert(ai_tensor_refcount(tensor) == 1); /* creator */

    ai_tensor_backing *backing = ai_backing_create_ram(
        tensor->bytes,
        AI_BACKING_ZEROED
    );
    assert(backing != NULL);
    assert(ai_backing_count() == 1);
    assert(ai_backing_is_live(backing));
    assert(ai_backing_refcount(backing) == 1); /* creator */

    assert(ai_tensor_attach_backing(tensor, backing, 0));
    assert(ai_backing_refcount(backing) == 2); /* creator + tensor */

    ai_handle_t owner = ai_handle_install(
        producer,
        tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ |
        AI_HANDLE_RIGHT_WRITE |
        AI_HANDLE_RIGHT_MAP |
        AI_HANDLE_RIGHT_SHARE
    );
    assert(owner != AI_HANDLE_INVALID);
    assert(ai_tensor_refcount(tensor) == 2); /* creator + owner handle */

    ai_handle_t shared = ai_handle_share(
        producer,
        owner,
        consumer,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP
    );
    assert(shared != AI_HANDLE_INVALID);
    assert(ai_tensor_refcount(tensor) == 3); /* + consumer handle */

    ai_mapping_id_t producer_map = ai_tensor_map(
        producer,
        owner,
        AI_MAP_PROT_READ | AI_MAP_PROT_WRITE
    );
    ai_mapping_id_t consumer_map = ai_tensor_map(
        consumer,
        shared,
        AI_MAP_PROT_READ
    );
    assert(producer_map != AI_MAPPING_INVALID);
    assert(consumer_map != AI_MAPPING_INVALID);

    assert(ai_tensor_refcount(tensor) == 5); /* creator + 2 handles + 2 maps */
    assert(ai_backing_refcount(backing) == 4); /* creator + tensor + 2 maps */
    assert(backing->mapping_count == 2);

    u32 *writer = (u32 *)ai_mapping_kernel_address(producer, producer_map);
    const u32 *reader = (const u32 *)ai_mapping_kernel_address(
        consumer,
        consumer_map
    );
    assert(writer != NULL && reader != NULL);

    /* Creator references can disappear while capabilities/mappings remain. */
    assert(ai_tensor_put(tensor));
    assert(ai_tensor_refcount(tensor) == 4);
    assert(ai_backing_put(backing));
    assert(ai_backing_refcount(backing) == 3);

    /* Closing all handles must not invalidate live mappings. */
    assert(ai_handle_close(producer, owner));
    assert(ai_handle_close(consumer, shared));
    assert(ai_tensor_refcount(tensor) == 2);
    assert(ai_tensor_is_live(tensor));
    assert(ai_backing_is_live(backing));

    writer[0] = 0x4e45584fu;
    assert(reader[0] == 0x4e45584fu);

    assert(ai_tensor_unmap(producer, producer_map));
    assert(ai_tensor_refcount(tensor) == 1);
    assert(ai_backing_refcount(backing) == 2);
    assert(backing->mapping_count == 1);
    assert(ai_tensor_is_live(tensor));

    /* Final mapping owns the final tensor reference and one backing reference. */
    assert(ai_tensor_unmap(consumer, consumer_map));
    assert(tensor->lifetime_state == AI_TENSOR_RETIRED);
    assert(tensor->refcount == 0);
    assert(backing->lifetime_state == AI_BACKING_RETIRED);
    assert(backing->refcount == 0);
    assert(backing->mapping_count == 0);
    assert(ai_tensor_count() == 0);
    assert(ai_backing_count() == 0);

    /* Retired objects and stale mappings cannot be resurrected or reused. */
    assert(!ai_tensor_get(tensor));
    assert(!ai_tensor_put(tensor));
    assert(!ai_backing_get(backing));
    assert(!ai_backing_put(backing));
    assert(ai_backing_kernel_at(backing, 0) == NULL);
    assert(ai_backing_physical_at(backing, 0) == 0);
    assert(ai_mapping_resolve(consumer, consumer_map) == NULL);
    assert(!ai_tensor_unmap(consumer, consumer_map));

    /* A retired tensor cannot be reinstalled into a handle namespace. */
    assert(ai_handle_install(
        producer,
        tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ
    ) == AI_HANDLE_INVALID);

    destroy_empty_domain(producer);
    destroy_empty_domain(consumer);
    return 0;
}
