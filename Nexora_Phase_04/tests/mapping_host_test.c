#include <assert.h>
#include <stdint.h>

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
    if (domain->state == AI_DOMAIN_ACTIVE) {
        assert(ai_domain_begin_quiesce(domain));
    }
    assert(ai_domain_destroy(domain));
}

int main(void) {
    early_heap_init();
    ai_backing_system_init();
    ai_tensor_system_init();
    ai_domain_system_init();

    ai_domain *producer = make_domain("producer");
    ai_domain *consumer = make_domain("consumer");

    u64 shape[1] = {2048}; /* 8192 bytes of f32 = two pages. */
    ai_tensor *tensor = ai_tensor_create(
        "shared-pages",
        AI_DTYPE_F32,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL
    );
    assert(tensor != NULL);
    assert(tensor->bytes == 8192);

    ai_tensor_backing *backing = ai_backing_create_ram(
        tensor->bytes,
        AI_BACKING_ZEROED
    );
    assert(backing != NULL);
    assert(backing->page_count == 2);
    assert(backing->mapping_count == 0);
    assert(ai_tensor_attach_backing(tensor, backing, 0));

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

    ai_handle_t shared = ai_handle_share(
        producer,
        owner,
        consumer,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP
    );
    assert(shared != AI_HANDLE_INVALID);

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

    assert(ai_mapping_live_count(producer) == 1);
    assert(ai_mapping_live_count(consumer) == 1);
    assert(backing->mapping_count == 2);

    /* Different domain-local virtual addresses, same physical backing. */
    u64 producer_va = ai_mapping_virtual_address(producer, producer_map);
    u64 consumer_va = ai_mapping_virtual_address(consumer, consumer_map);
    assert(producer_va != 0 && consumer_va != 0);
    assert(producer_va != consumer_va);

    u64 producer_pa = ai_mapping_physical_address(producer, producer_map);
    u64 consumer_pa = ai_mapping_physical_address(consumer, consumer_map);
    assert(producer_pa != 0);
    assert(producer_pa == consumer_pa);
    assert(producer_pa == backing->physical_base);

    /* Kernel-side access proves both mappings point at exactly the same bytes. */
    u32 *producer_ptr = (u32 *)ai_mapping_kernel_address(
        producer,
        producer_map
    );
    const u32 *consumer_ptr = (const u32 *)ai_mapping_kernel_address(
        consumer,
        consumer_map
    );
    assert(producer_ptr != NULL && consumer_ptr != NULL);
    assert((const void *)producer_ptr == (const void *)consumer_ptr);

    producer_ptr[0] = 0x11223344u;
    producer_ptr[1024] = 0xa5a55a5au;
    assert(consumer_ptr[0] == 0x11223344u);
    assert(consumer_ptr[1024] == 0xa5a55a5au);

    /* Translation preserves byte offsets into the shared backing. */
    u64 translated = 0;
    assert(ai_mapping_translate(
        consumer,
        consumer_map,
        consumer_va + 4096,
        &translated
    ));
    assert(translated == backing->physical_base + 4096);
    assert(!ai_mapping_translate(
        consumer,
        consumer_map,
        consumer_va + tensor->bytes,
        &translated
    ));

    /* The read-only delegated handle cannot create a writable mapping. */
    assert(ai_tensor_map(
        consumer,
        shared,
        AI_MAP_PROT_READ | AI_MAP_PROT_WRITE
    ) == AI_MAPPING_INVALID);

    /* MAP authority is mandatory even when READ exists. */
    ai_handle_t read_only = ai_handle_install(
        producer,
        tensor,
        AI_HANDLE_OBJECT_TENSOR,
        AI_HANDLE_RIGHT_READ
    );
    assert(read_only != AI_HANDLE_INVALID);
    assert(ai_tensor_map(
        producer,
        read_only,
        AI_MAP_PROT_READ
    ) == AI_MAPPING_INVALID);
    assert(ai_handle_close(producer, read_only));

    /* Page-offset rules prevent unrepresentable mappings in this prototype. */
    assert(ai_tensor_map_range(
        producer,
        owner,
        1,
        64,
        AI_MAP_PROT_READ
    ) == AI_MAPPING_INVALID);

    ai_mapping_id_t second_page = ai_tensor_map_range(
        producer,
        owner,
        4096,
        4096,
        AI_MAP_PROT_READ
    );
    assert(second_page != AI_MAPPING_INVALID);
    assert(ai_mapping_physical_address(producer, second_page) ==
           backing->physical_base + 4096);
    assert(backing->mapping_count == 3);

    /* A foreign domain cannot resolve or unmap another domain's mapping ID. */
    assert(ai_mapping_resolve(consumer, producer_map) == NULL);
    assert(!ai_tensor_unmap(consumer, producer_map));

    /* Domain destruction is blocked while mappings are still live. */
    assert(ai_handle_close(producer, owner));
    assert(ai_handle_close(consumer, shared));
    assert(ai_domain_begin_quiesce(producer));
    assert(ai_domain_begin_quiesce(consumer));
    assert(!ai_domain_destroy(producer));
    assert(!ai_domain_destroy(consumer));

    /* Existing mappings can be drained during QUIESCING. */
    assert(ai_tensor_unmap(producer, second_page));
    assert(ai_tensor_unmap(producer, producer_map));
    assert(ai_tensor_unmap(consumer, consumer_map));
    assert(backing->mapping_count == 0);
    assert(ai_mapping_live_count(producer) == 0);
    assert(ai_mapping_live_count(consumer) == 0);

    /* Closed mapping IDs are stale after generation advancement. */
    assert(ai_mapping_resolve(producer, producer_map) == NULL);
    assert(!ai_tensor_unmap(producer, producer_map));

    destroy_empty_domain(producer);
    destroy_empty_domain(consumer);

    return 0;
}
