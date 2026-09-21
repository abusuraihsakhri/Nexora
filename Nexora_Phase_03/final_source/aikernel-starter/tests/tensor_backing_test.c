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

int main(void) {
    early_heap_init();
    nx_object_system_init();
    nx_memory_system_init();
    ai_tensor_system_init();

    u64 shape[2] = {4, 8};
    ai_tensor *tensor = ai_tensor_create(
        "activation",
        AI_DTYPE_F32,
        AI_TENSOR_CLASS_ACTIVATION,
        2,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL | AI_TENSOR_ZERO_INIT
    );

    check(!ai_tensor_is_backed(tensor), "tensor begins unbacked");
    check(ai_tensor_allocate_backing(tensor, 64) == AI_TENSOR_OK,
          "allocate tensor backing");
    check(ai_tensor_is_backed(tensor), "tensor reports backing");

    nx_memory *memory = ai_tensor_backing(tensor);
    check(memory != NULL, "backing lookup");
    check(memory->object.type == NX_OBJECT_MEMORY, "backing object type");
    check(memory->backend == NX_MEMORY_BACKEND_EARLY_HEAP, "backing backend");
    check(memory->size_bytes == 128, "requested backing bytes");
    check(memory->capacity_bytes == NX_MEMORY_PAGE_SIZE, "page-rounded capacity");
    check(memory->alignment == NX_MEMORY_PAGE_SIZE, "page-aligned backing");
    check(((u64)(usize)memory->kernel_address % NX_MEMORY_PAGE_SIZE) == 0,
          "page aligned backing pointer");

    u8 *data = (u8 *)ai_tensor_data(tensor);
    check(data != NULL, "tensor data pointer");
    for (u64 i = 0; i < tensor->storage_span_bytes; ++i) {
        check(data[i] == 0, "zero-init backing");
    }
    data[0] = 0x5a;
    data[127] = 0xa5;
    check(((u8 *)memory->kernel_address)[0] == 0x5a, "tensor writes backing start");
    check(((u8 *)memory->kernel_address)[127] == 0xa5, "tensor writes backing end");

    u64 view_shape[1] = {16};
    ai_tensor_desc view_desc = {
        .name = "view",
        .dtype = AI_DTYPE_F32,
        .tensor_class = AI_TENSOR_CLASS_SCRATCH,
        .ndim = 1,
        .shape = view_shape,
        .stride_bytes = NULL,
        .location = AI_LOC_CPU_RAM,
        .lifetime = AI_TENSOR_LIFETIME_TEMPORARY,
        .flags = AI_TENSOR_EPHEMERAL | AI_TENSOR_VIEW
    };

    ai_tensor *view = NULL;
    check(ai_tensor_try_create(&view_desc, &view) == AI_TENSOR_OK, "create view");
    check(ai_tensor_allocate_backing(view, 64) == AI_TENSOR_ERR_VIEW_REQUIRES_EXISTING_BACKING,
          "view refuses private allocation");
    check(ai_tensor_bind_memory(view, memory->object.handle, 64) == AI_TENSOR_OK,
          "bind view into existing memory object");
    check(ai_tensor_data(view) == (void *)((u8 *)memory->kernel_address + 64),
          "view offset data pointer");

    nx_memory *small = nx_memory_create("small", 16, 64, NX_MEMORY_FLAG_NONE);
    u64 large_view_shape[1] = {2048};
    ai_tensor_desc large_view_desc = view_desc;
    large_view_desc.name = "large-view";
    large_view_desc.shape = large_view_shape;
    ai_tensor *large_view = NULL;
    check(ai_tensor_try_create(&large_view_desc, &large_view) == AI_TENSOR_OK,
          "create large view");
    check(ai_tensor_bind_memory(large_view, small->object.handle, 0) == AI_TENSOR_ERR_BACKING_TOO_SMALL,
          "reject undersized backing");

    const nx_memory_stats *stats = nx_memory_get_stats();
    check(stats->created == 2, "memory object count");
    check(stats->resident_bytes == 2 * NX_MEMORY_PAGE_SIZE,
          "resident page accounting");
    check(stats->requested_bytes == 144, "requested byte accounting");

    puts("tensor backing tests: PASS");
    return 0;
}
