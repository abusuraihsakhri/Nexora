#include <ai/tensor.h>
#include <kernel/memory.h>
#include <kernel/memory_object.h>
#include <kernel/object.h>
#include <kernel/panic.h>

#include <stdio.h>
#include <stdlib.h>

#define TEST_ARENA_SIZE (512u * 1024u)

static u8 test_arena[TEST_ARENA_SIZE] __attribute__((aligned(4096)));
static usize test_offset;
static u32 probe_destroy_count;

void early_heap_init(void) {
    test_offset = 0;
}

void *kalloc_try(usize size, usize alignment) {
    if (alignment == 0) {
        alignment = 1;
    }
    if ((alignment & (alignment - 1u)) != 0) {
        return NULL;
    }

    usize aligned = (test_offset + alignment - 1u) & ~(alignment - 1u);
    if (size > TEST_ARENA_SIZE || aligned > TEST_ARENA_SIZE - size) {
        return NULL;
    }

    void *ptr = &test_arena[aligned];
    test_offset = aligned + size;
    return ptr;
}

void *kalloc(usize size, usize alignment) {
    void *ptr = kalloc_try(size, alignment);
    if (ptr == NULL) {
        panic("test arena exhausted");
    }
    return ptr;
}

usize early_heap_used(void) {
    return test_offset;
}

usize early_heap_capacity(void) {
    return TEST_ARENA_SIZE;
}

usize early_heap_remaining(void) {
    return TEST_ARENA_SIZE - test_offset;
}

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

static void probe_destroy(nx_object *object) {
    (void)object;
    ++probe_destroy_count;
}

int main(void) {
    early_heap_init();
    nx_object_system_init();
    nx_memory_system_init();
    ai_tensor_system_init();

    /* Generic pinning delays final destruction after the last strong ref. */
    nx_object probe;
    check(nx_object_register_ex(
              &probe,
              NX_OBJECT_DEVICE,
              "lifetime-probe",
              NX_OBJECT_FLAG_KERNEL,
              11,
              probe_destroy),
          "register lifetime probe");
    nx_handle_t probe_handle = probe.handle;
    check(nx_object_ref_count(&probe) == 1, "probe initial owner reference");
    check(nx_object_pin(probe_handle), "pin probe");
    check(nx_object_release(probe_handle), "release probe owner reference");
    check(probe.state == NX_OBJECT_QUIESCING, "pinned zero-ref object quiesces");
    check(nx_object_lookup(probe_handle, NX_OBJECT_DEVICE) == &probe,
          "quiescing pinned object remains addressable");
    check(!nx_object_retain(probe_handle), "cannot resurrect quiescing object");
    check(nx_object_unpin(probe_handle), "unpin probe");
    check(probe_destroy_count == 1, "destructor called once");
    check(nx_object_lookup(probe_handle, NX_OBJECT_DEVICE) == NULL,
          "stale probe handle rejected after destruction");

    nx_memory *shared = nx_memory_create_owned(
        "shared-backing", 256, 64, NX_MEMORY_FLAG_NONE, 7
    );
    nx_handle_t shared_handle = shared->object.handle;
    check(shared->object.owner_id == 7, "memory owner recorded");
    check(!nx_object_transfer_owner(shared_handle, 99, 42),
          "owner transfer rejects wrong expected owner");
    check(nx_object_transfer_owner(shared_handle, 7, 42),
          "memory owner transfer");
    check(shared->object.owner_id == 42, "new memory owner recorded");

    u64 shape[1] = {16};
    ai_tensor *a = ai_tensor_create_owned(
        "view-a",
        AI_DTYPE_F32,
        AI_TENSOR_CLASS_ACTIVATION,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL | AI_TENSOR_VIEW,
        42
    );
    ai_tensor *b = ai_tensor_create_owned(
        "view-b",
        AI_DTYPE_F32,
        AI_TENSOR_CLASS_ACTIVATION,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL | AI_TENSOR_VIEW | AI_TENSOR_PINNED,
        42
    );

    check(ai_tensor_bind_memory(a, shared_handle, 0) == AI_TENSOR_OK,
          "bind first tensor");
    check(ai_tensor_bind_memory(b, shared_handle, 64) == AI_TENSOR_OK,
          "bind pinned second tensor");
    check(nx_object_ref_count(&shared->object) == 3,
          "creator plus two tensor strong references");
    check(nx_object_pin_count(&shared->object) == 1,
          "pinned tensor contributes one memory pin");

    check(nx_memory_release(shared_handle), "drop memory creator reference");
    check(nx_object_ref_count(&shared->object) == 2,
          "tensor references keep shared memory alive");

    nx_handle_t a_handle = a->object.handle;
    nx_handle_t b_handle = b->object.handle;
    check(ai_tensor_release(b), "release pinned tensor");
    check(nx_object_lookup(b_handle, NX_OBJECT_TENSOR) == NULL,
          "released tensor handle becomes stale");
    check(nx_memory_lookup(shared_handle) == shared,
          "shared memory survives while another tensor references it");
    check(nx_object_ref_count(&shared->object) == 1,
          "one tensor reference remains");
    check(nx_object_pin_count(&shared->object) == 0,
          "tensor destruction removes backing pin");

    check(ai_tensor_release(a), "release final tensor");
    check(nx_object_lookup(a_handle, NX_OBJECT_TENSOR) == NULL,
          "final tensor retired");
    check(nx_memory_lookup(shared_handle) == NULL,
          "shared memory retires after final tensor reference");

    const ai_tensor_stats *tensor_stats = ai_tensor_get_stats();
    check(tensor_stats->destroyed == 2, "tensor destruction statistics");
    check(tensor_stats->live == 0, "no live tensors remain");
    check(tensor_stats->logical_bytes == 0, "destroyed tensor bytes removed");
    check(tensor_stats->backing_refs_acquired == 2, "backing refs acquired");
    check(tensor_stats->backing_refs_released == 2, "backing refs released");

    const nx_memory_stats *memory_stats = nx_memory_get_stats();
    check(memory_stats->created == 1, "one shared memory object created");
    check(memory_stats->destroyed == 1, "shared memory object destroyed");
    check(memory_stats->live == 0, "no logically live memory remains");
    check(memory_stats->resident_bytes == 0, "live resident accounting reclaimed");
    check(memory_stats->peak_resident_bytes == NX_MEMORY_PAGE_SIZE,
          "peak resident accounting retained");
    check(early_heap_used() > 0, "bootstrap arena remains monotonic");

    puts("ownership/refcount tests: PASS");
    return 0;
}
