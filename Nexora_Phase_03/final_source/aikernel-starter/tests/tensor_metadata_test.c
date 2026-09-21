#include <ai/tensor.h>
#include <kernel/memory.h>
#include <kernel/object.h>
#include <kernel/panic.h>

#include <stdio.h>
#include <stdlib.h>

#define TEST_ARENA_SIZE (64u * 1024u)

static u8 test_arena[TEST_ARENA_SIZE];
static usize test_offset;

void early_heap_init(void) {
    test_offset = 0;
}

void *kalloc_try(usize size, usize alignment) {
    if (alignment == 0) {
        alignment = 1;
    }

    usize aligned = (test_offset + alignment - 1) & ~(alignment - 1);
    if (aligned + size > TEST_ARENA_SIZE) {
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

    u64 contiguous_shape[3] = {2, 3, 4};
    ai_tensor *contiguous = ai_tensor_create(
        "contiguous",
        AI_DTYPE_F16,
        AI_TENSOR_CLASS_ACTIVATION,
        3,
        contiguous_shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL
    );

    check(contiguous->element_count == 24, "contiguous element count");
    check(contiguous->logical_bytes == 48, "contiguous logical bytes");
    check(contiguous->storage_span_bytes == 48, "contiguous storage span");
    check(contiguous->stride_bytes[0] == 24, "contiguous stride 0");
    check(contiguous->stride_bytes[1] == 8, "contiguous stride 1");
    check(contiguous->stride_bytes[2] == 2, "contiguous stride 2");
    check(ai_tensor_is_contiguous(contiguous), "contiguous layout detection");
    check(!ai_tensor_is_backed(contiguous), "new tensor must be unbacked");
    check(nx_object_lookup(contiguous->object.handle, NX_OBJECT_TENSOR) == &contiguous->object,
          "tensor handle lookup");

    u64 strided_shape[2] = {2, 3};
    u64 strided_bytes[2] = {16, 4};
    ai_tensor_desc strided_desc = {
        .name = "strided",
        .dtype = AI_DTYPE_F32,
        .tensor_class = AI_TENSOR_CLASS_SCRATCH,
        .ndim = 2,
        .shape = strided_shape,
        .stride_bytes = strided_bytes,
        .location = AI_LOC_CPU_RAM,
        .lifetime = AI_TENSOR_LIFETIME_TEMPORARY,
        .flags = AI_TENSOR_EPHEMERAL | AI_TENSOR_VIEW
    };

    ai_tensor *strided = NULL;
    check(ai_tensor_try_create(&strided_desc, &strided) == AI_TENSOR_OK,
          "strided tensor create");
    check(strided != NULL, "strided tensor pointer");
    check(strided->layout == AI_TENSOR_LAYOUT_STRIDED, "strided layout detection");
    check(strided->logical_bytes == 24, "strided logical bytes");
    check(strided->storage_span_bytes == 28, "strided storage span");

    u64 overflow_shape[2] = {~(u64)0, 2};
    ai_tensor_desc overflow_desc = {
        .name = "overflow",
        .dtype = AI_DTYPE_F32,
        .tensor_class = AI_TENSOR_CLASS_SCRATCH,
        .ndim = 2,
        .shape = overflow_shape,
        .stride_bytes = NULL,
        .location = AI_LOC_CPU_RAM,
        .lifetime = AI_TENSOR_LIFETIME_TEMPORARY,
        .flags = AI_TENSOR_EPHEMERAL
    };

    ai_tensor *invalid = NULL;
    check(ai_tensor_try_create(&overflow_desc, &invalid) == AI_TENSOR_ERR_SHAPE_OVERFLOW,
          "shape overflow rejection");
    check(invalid == NULL, "overflow must not return tensor");

    u64 zero_shape[2] = {4, 0};
    ai_tensor_desc zero_desc = overflow_desc;
    zero_desc.name = "zero";
    zero_desc.shape = zero_shape;
    check(ai_tensor_try_create(&zero_desc, &invalid) == AI_TENSOR_ERR_ZERO_EXTENT,
          "zero extent rejection");

    u64 small_shape[1] = {8};
    ai_tensor_desc flags_desc = overflow_desc;
    flags_desc.name = "bad-flags";
    flags_desc.ndim = 1;
    flags_desc.shape = small_shape;
    flags_desc.flags = AI_TENSOR_PERSISTENT | AI_TENSOR_EPHEMERAL;
    check(ai_tensor_try_create(&flags_desc, &invalid) == AI_TENSOR_ERR_INVALID_FLAGS,
          "conflicting lifetime flags rejection");

    const ai_tensor_stats *stats = ai_tensor_get_stats();
    check(stats->created == 2, "created statistics");
    check(stats->live == 2, "live statistics");
    check(stats->logical_bytes == 72, "logical-byte statistics");
    check(stats->peak_logical_bytes == 72, "peak logical-byte statistics");

    puts("tensor metadata tests: PASS");
    return 0;
}
