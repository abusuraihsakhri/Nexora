#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <ai/backing.h>
#include <ai/domain.h>
#include <ai/handle.h>
#include <ai/mapping.h>
#include <ai/tensor.h>
#include <ai/work.h>
#include <ai/scheduler.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

#define TEST_ARENA_SIZE (8u * 1024u * 1024u)

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

static ai_domain *make_domain(const char *name) {
    ai_domain_limits unlimited = {0, 0, 0};
    ai_domain *domain = ai_domain_create(name, 0, unlimited);
    assert(domain != NULL);
    assert(ai_domain_activate(domain));
    return domain;
}

static void destroy_domain(ai_domain *domain) {
    assert(domain != NULL);
    assert(ai_handle_live_count(domain) == 0);
    assert(ai_mapping_live_count(domain) == 0);
    if (ai_domain_state_get(domain) == AI_DOMAIN_ACTIVE) {
        assert(ai_domain_begin_quiesce(domain));
    }
    assert(ai_domain_destroy(domain));
}

int main(void) {
    early_heap_init();
    ai_backing_system_init();
    ai_tensor_system_init();
    ai_domain_system_init();

    ai_domain *producer = make_domain("phase4-producer");
    ai_domain *consumer = make_domain("phase4-consumer");
    ai_domain *receiver = make_domain("phase4-receiver");

    u64 shape[1] = {4096}; /* 16 KiB f32 */
    ai_tensor *tensor = ai_tensor_create(
        "phase4-e2e",
        AI_DTYPE_F32,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL);
    assert(tensor != NULL);

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
        AI_HANDLE_RIGHT_SHARE |
        AI_HANDLE_RIGHT_TRANSFER);
    assert(owner != AI_HANDLE_INVALID);

    /* Step 5: attenuated cross-domain share. */
    ai_handle_t consumer_handle = ai_handle_share(
        producer,
        owner,
        consumer,
        AI_HANDLE_RIGHT_READ |
        AI_HANDLE_RIGHT_MAP |
        AI_HANDLE_RIGHT_SHARE);
    assert(consumer_handle != AI_HANDLE_INVALID);
    assert(!ai_handle_has_rights(consumer, consumer_handle, AI_HANDLE_RIGHT_WRITE));

    /* Step 6: same backing, distinct domain-local virtual mappings. */
    ai_mapping_id_t producer_map = ai_tensor_map(
        producer, owner, AI_MAP_PROT_READ | AI_MAP_PROT_WRITE);
    ai_mapping_id_t consumer_map = ai_tensor_map(
        consumer, consumer_handle, AI_MAP_PROT_READ);
    assert(producer_map != AI_MAPPING_INVALID);
    assert(consumer_map != AI_MAPPING_INVALID);
    assert(ai_mapping_virtual_address(producer, producer_map) !=
           ai_mapping_virtual_address(consumer, consumer_map));
    assert(ai_mapping_physical_address(producer, producer_map) ==
           ai_mapping_physical_address(consumer, consumer_map));
    assert(ai_mapping_physical_address(producer, producer_map) ==
           backing->physical_base);

    u32 *writer = (u32 *)ai_mapping_kernel_address(producer, producer_map);
    const u32 *reader = (const u32 *)ai_mapping_kernel_address(consumer, consumer_map);
    assert(writer != NULL && reader != NULL);
    writer[0] = 0x13579bdfu;
    writer[2048] = 0x2468ace0u;
    assert(reader[0] == 0x13579bdfu);
    assert(reader[2048] == 0x2468ace0u);

    /* Rights stay enforced after delegation. */
    assert(ai_tensor_map(
        consumer,
        consumer_handle,
        AI_MAP_PROT_READ | AI_MAP_PROT_WRITE) == AI_MAPPING_INVALID);

    /* Transitive share is possible only because SHARE was explicitly delegated. */
    ai_handle_t receiver_read = ai_handle_share(
        consumer,
        consumer_handle,
        receiver,
        AI_HANDLE_RIGHT_READ | AI_HANDLE_RIGHT_MAP);
    assert(receiver_read != AI_HANDLE_INVALID);
    ai_mapping_id_t receiver_map = ai_tensor_map(
        receiver, receiver_read, AI_MAP_PROT_READ);
    assert(receiver_map != AI_MAPPING_INVALID);
    assert(ai_mapping_physical_address(receiver, receiver_map) == backing->physical_base);

    /* Scheduler integration: producer/consumer work nodes reference the same tensor. */
    ai_work_graph graph;
    ai_work_graph_init(&graph);
    ai_work_node *produce = ai_work_add(
        &graph, "produce-shared", AI_OP_CUSTOM, 100, 1000, AI_DEVICE_CPU);
    ai_work_add_output(produce, tensor);
    ai_work_node *consume = ai_work_add(
        &graph, "consume-shared", AI_OP_CUSTOM, 90, 2000, AI_DEVICE_CPU);
    ai_work_add_dependency(consume, produce->id);
    ai_work_add_input(consume, tensor);

    ai_scheduler scheduler;
    ai_scheduler_init(&scheduler, &graph);
    assert(ai_scheduler_pick(&scheduler) == produce);
    ai_scheduler_mark_running(&scheduler, produce);
    ai_scheduler_mark_done(&scheduler, produce);
    assert(ai_scheduler_pick(&scheduler) == consume);
    ai_scheduler_mark_running(&scheduler, consume);
    ai_scheduler_mark_done(&scheduler, consume);
    assert(scheduler.dispatch_count == 2);
    assert(consume->inputs[0] == produce->outputs[0]);

    /* TRANSFER moves namespace authority but established mappings remain valid. */
    ai_handle_t receiver_owner = ai_handle_transfer(
        producer,
        owner,
        receiver,
        AI_HANDLE_RIGHT_READ |
        AI_HANDLE_RIGHT_WRITE |
        AI_HANDLE_RIGHT_MAP |
        AI_HANDLE_RIGHT_SHARE);
    assert(receiver_owner != AI_HANDLE_INVALID);
    assert(!ai_handle_is_valid(
        producer, owner, AI_HANDLE_OBJECT_TENSOR, AI_HANDLE_RIGHT_READ));
    assert(ai_handle_is_valid(
        receiver, receiver_owner, AI_HANDLE_OBJECT_TENSOR, AI_HANDLE_RIGHT_WRITE));
    assert(writer[0] == 0x13579bdfu);

    /* Step 8: local revocation blocks new acquisitions but not existing mappings. */
    assert(ai_handle_revoke(consumer, consumer_handle));
    assert(ai_handle_is_revoked(consumer, consumer_handle));
    assert(ai_tensor_map(
        consumer, consumer_handle, AI_MAP_PROT_READ) == AI_MAPPING_INVALID);
    assert(reader[2048] == 0x2468ace0u);
    assert(ai_handle_reap_revoked(consumer) == 1);

    /* Local-token revocation is intentionally non-transitive. */
    assert(ai_handle_is_valid(
        receiver, receiver_read, AI_HANDLE_OBJECT_TENSOR, AI_HANDLE_RIGHT_READ));

    /* Stable mapping views survive concurrent-style namespace removal. */
    ai_mapping_view view = {0};
    assert(ai_mapping_acquire_view(receiver, receiver_map, &view));
    assert(view.valid);
    assert(view.physical_base == backing->physical_base);
    assert(ai_tensor_unmap(receiver, receiver_map));
    assert(((const u32 *)view.kernel_base)[0] == 0x13579bdfu);
    ai_mapping_release_view(&view);

    /* Drop creation references. Remaining handles/mappings now own lifetime. */
    assert(ai_tensor_put(tensor));
    assert(ai_backing_put(backing));
    assert(ai_tensor_is_live(tensor));
    assert(ai_backing_is_live(backing));

    assert(ai_handle_close(receiver, receiver_read));
    assert(ai_handle_close(receiver, receiver_owner));

    /* Mapping ownership keeps payload alive after all handles are gone. */
    assert(ai_tensor_is_live(tensor));
    assert(ai_backing_is_live(backing));
    assert(ai_backing_mapping_count(backing) == 2);

    assert(ai_tensor_unmap(consumer, consumer_map));
    assert(ai_tensor_is_live(tensor));
    assert(ai_tensor_unmap(producer, producer_map));

    /* Final legitimate reference retires both metadata and backing exactly once. */
    assert(!ai_tensor_is_live(tensor));
    assert(!ai_backing_is_live(backing));
    assert(ai_tensor_count() == 0);
    assert(ai_backing_count() == 0);

    destroy_domain(producer);
    destroy_domain(consumer);
    destroy_domain(receiver);
    assert(ai_domain_count() == 0);

    puts("Phase 4 end-to-end integration gate: PASS");
    return 0;
}
