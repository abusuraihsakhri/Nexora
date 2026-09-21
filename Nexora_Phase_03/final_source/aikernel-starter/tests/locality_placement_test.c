#include <ai/device.h>
#include <ai/tensor.h>
#include <ai/work.h>
#include <ai/scheduler.h>
#include <kernel/memory.h>
#include <kernel/object.h>
#include <kernel/memory_object.h>
#include <kernel/panic.h>

#include <stdio.h>
#include <stdlib.h>

#define TEST_ARENA_SIZE (4u * 1024u * 1024u)

static u8 test_arena[TEST_ARENA_SIZE] __attribute__((aligned(4096)));
static usize test_offset;

void early_heap_init(void) { test_offset = 0; }

void *kalloc_try(usize size, usize alignment) {
    if (alignment == 0) alignment = 1;
    if ((alignment & (alignment - 1u)) != 0) return NULL;
    usize aligned = (test_offset + alignment - 1u) & ~(alignment - 1u);
    if (size > TEST_ARENA_SIZE || aligned > TEST_ARENA_SIZE - size) return NULL;
    void *ptr = &test_arena[aligned];
    test_offset = aligned + size;
    return ptr;
}

void *kalloc(usize size, usize alignment) {
    void *ptr = kalloc_try(size, alignment);
    if (ptr == NULL) panic("test arena exhausted");
    return ptr;
}

usize early_heap_used(void) { return test_offset; }
usize early_heap_capacity(void) { return TEST_ARENA_SIZE; }
usize early_heap_remaining(void) { return TEST_ARENA_SIZE - test_offset; }

void panic(const char *message) {
    fprintf(stderr, "panic: %s\n", message ? message : "(null)");
    exit(2);
}

static void require_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static ai_work_desc make_desc(const char *name, u32 mask) {
    ai_work_desc desc;
    desc.name = name;
    desc.op = AI_OP_CUSTOM;
    desc.work_class = AI_WORK_CLASS_COMPUTE;
    desc.qos = AI_WORK_QOS_DEFAULT;
    desc.priority = 10;
    desc.deadline_ns = AI_WORK_NO_DEADLINE;
    desc.device_mask = mask;
    desc.preferred_device = AI_DEVICE_INVALID;
    desc.estimated_ops = 0;
    desc.estimated_read_bytes = 0;
    desc.estimated_write_bytes = 0;
    desc.estimated_scratch_bytes = 0;
    desc.estimated_duration_ns = 0;
    desc.batch_id = AI_WORK_NO_BATCH;
    desc.batch_size = 1;
    desc.flags = AI_WORK_FLAG_AUTO_IO_BYTES;
    return desc;
}

static void release_tensor(ai_tensor *tensor) {
    require_true(tensor != NULL && ai_tensor_release(tensor), "tensor release failed");
}

int main(void) {
    early_heap_init();
    nx_object_system_init();
    ai_device_system_init();
    nx_memory_system_init();
    ai_tensor_system_init();

    ai_device *cpu = ai_device_default(AI_DEVICE_CPU);
    ai_device *gpu = ai_device_default(AI_DEVICE_GPU);
    ai_device *npu = ai_device_default(AI_DEVICE_NPU);
    require_true(cpu != NULL && gpu != NULL && npu != NULL, "default devices missing");
    require_true(ai_device_count() == 3, "unexpected synthetic device count");

    u64 transfer_ns = 0;
    require_true(
        ai_device_estimate_transfer_ns(cpu->object.handle, gpu->object.handle,
                                       4ull * 1024ull * 1024ull, &transfer_ns) == AI_DEVICE_OK &&
        transfer_ns > 2000,
        "CPU->GPU transfer estimate invalid"
    );

    u64 shape[1] = {1024};
    ai_tensor *cpu_input = ai_tensor_create_with_lifetime(
        "cpu-input", AI_DTYPE_F32, AI_TENSOR_CLASS_INPUT, 1, shape,
        AI_LOC_CPU_RAM, AI_TENSOR_LIFETIME_TEMPORARY, 0
    );
    ai_tensor *gpu_weight = ai_tensor_create_with_lifetime(
        "gpu-weight", AI_DTYPE_F32, AI_TENSOR_CLASS_WEIGHT, 1, shape,
        AI_LOC_GPU_HBM, AI_TENSOR_LIFETIME_PERSISTENT, AI_TENSOR_READONLY
    );
    ai_tensor *gpu_output = ai_tensor_create_with_lifetime(
        "gpu-output", AI_DTYPE_F32, AI_TENSOR_CLASS_OUTPUT, 1, shape,
        AI_LOC_GPU_HBM, AI_TENSOR_LIFETIME_TEMPORARY, 0
    );

    require_true(ai_tensor_allocate_backing(cpu_input, NX_MEMORY_PAGE_SIZE) == AI_TENSOR_OK,
                 "CPU input backing failed");
    require_true(ai_tensor_allocate_backing(gpu_weight, NX_MEMORY_PAGE_SIZE) == AI_TENSOR_OK,
                 "GPU weight backing failed");
    require_true(ai_tensor_allocate_backing(gpu_output, NX_MEMORY_PAGE_SIZE) == AI_TENSOR_OK,
                 "GPU output backing failed");
    require_true(cpu_input->resident_device == cpu->object.handle,
                 "CPU tensor residency not initialized");
    require_true(gpu_weight->resident_device == gpu->object.handle,
                 "GPU tensor residency not initialized");

    ai_work_graph graph;
    ai_work_graph_init(&graph);
    ai_work_desc desc = make_desc("mixed-locality", AI_DEVICE_CPU | AI_DEVICE_GPU);
    desc.estimated_ops = 1000000000ull;
    ai_work_node *mixed = ai_work_create(&graph, &desc);
    ai_work_add_input(mixed, cpu_input);
    ai_work_add_input(mixed, gpu_weight);
    ai_work_add_output(mixed, gpu_output);
    ai_work_refresh_states(&graph);

    ai_dispatch_choice choice;
    require_true(ai_placement_choose(mixed, &choice) == AI_PLACEMENT_OK,
                 "placement chooser failed");
    require_true(choice.device_id == gpu->object.handle,
                 "GPU should win mixed-locality compute placement");
    require_true(choice.input_transfers == 1 && choice.input_transfer_bytes == cpu_input->logical_bytes,
                 "GPU choice should transfer only CPU input");

    ai_scheduler scheduler;
    ai_scheduler_init(&scheduler, &graph);
    ai_dispatch_choice dispatch;
    require_true(ai_scheduler_pick_dispatch(&scheduler, &dispatch) == AI_PLACEMENT_OK,
                 "scheduler dispatch placement failed");
    require_true(dispatch.work == mixed && dispatch.device_id == gpu->object.handle,
                 "scheduler selected wrong work/device pair");
    require_true(ai_scheduler_mark_running_on(&scheduler, mixed, dispatch.device_id) == AI_WORK_OK,
                 "mark running on chosen device failed");
    require_true(mixed->selected_device == gpu->object.handle,
                 "selected device not recorded on work object");
    require_true(ai_scheduler_complete(&scheduler, mixed) == AI_WORK_OK,
                 "scheduler completion failed");
    require_true(gpu_output->resident_device == gpu->object.handle,
                 "output residency not committed to GPU");

    ai_work_graph_destroy(&graph);
    release_tensor(cpu_input);
    release_tensor(gpu_weight);
    release_tensor(gpu_output);

    /* A tiny CPU-local operation should stay on CPU because transfers dominate. */
    ai_tensor *local_in = ai_tensor_create_with_lifetime(
        "local-in", AI_DTYPE_F32, AI_TENSOR_CLASS_INPUT, 1, shape,
        AI_LOC_CPU_RAM, AI_TENSOR_LIFETIME_TEMPORARY, 0
    );
    ai_tensor *local_out = ai_tensor_create_with_lifetime(
        "local-out", AI_DTYPE_F32, AI_TENSOR_CLASS_OUTPUT, 1, shape,
        AI_LOC_CPU_RAM, AI_TENSOR_LIFETIME_TEMPORARY, 0
    );
    require_true(ai_tensor_allocate_backing(local_in, NX_MEMORY_PAGE_SIZE) == AI_TENSOR_OK,
                 "local input backing failed");
    require_true(ai_tensor_allocate_backing(local_out, NX_MEMORY_PAGE_SIZE) == AI_TENSOR_OK,
                 "local output backing failed");

    ai_work_graph_init(&graph);
    desc = make_desc("cpu-local", AI_DEVICE_CPU | AI_DEVICE_GPU);
    desc.estimated_ops = 1;
    ai_work_node *local = ai_work_create(&graph, &desc);
    ai_work_add_input(local, local_in);
    ai_work_add_output(local, local_out);
    require_true(ai_placement_choose(local, &choice) == AI_PLACEMENT_OK,
                 "local placement failed");
    require_true(choice.device_id == cpu->object.handle && choice.input_transfers == 0,
                 "CPU-local operation should remain on CPU");
    ai_work_graph_destroy(&graph);
    release_tensor(local_in);
    release_tensor(local_out);

    /* Preferred device is a deterministic tie-break, not an arbitrary cost penalty. */
    ai_work_graph_init(&graph);
    desc = make_desc("preference-tie", AI_DEVICE_CPU | AI_DEVICE_GPU);
    desc.preferred_device = gpu->object.handle;
    ai_work_node *preferred = ai_work_create(&graph, &desc);
    require_true(ai_placement_choose(preferred, &choice) == AI_PLACEMENT_OK,
                 "preferred-device placement failed");
    require_true(choice.device_id == gpu->object.handle && choice.preferred_device,
                 "preferred device did not win equal-cost placement");
    require_true(ai_work_set_preferred_device(preferred, npu->object.handle) ==
                     AI_WORK_ERR_INVALID_PREFERRED_DEVICE,
                 "disallowed preferred device accepted");
    ai_work_graph_destroy(&graph);

    /* Hard device-class constraints are enforced. */
    ai_work_graph_init(&graph);
    desc = make_desc("gpu-only", AI_DEVICE_GPU);
    ai_work_node *gpu_only = ai_work_create(&graph, &desc);
    require_true(ai_placement_evaluate(gpu_only, cpu->object.handle, &choice) ==
                     AI_PLACEMENT_ERR_DEVICE_NOT_ALLOWED,
                 "CPU accepted for GPU-only work");
    require_true(ai_placement_choose(gpu_only, &choice) == AI_PLACEMENT_OK &&
                     choice.device_id == gpu->object.handle,
                 "GPU-only work did not choose GPU");
    ai_work_graph_destroy(&graph);

    const ai_device_stats *device_stats = ai_device_get_stats();
    require_true(device_stats->devices_registered == 3, "device stats registration mismatch");
    require_true(device_stats->transfer_estimates > 0, "transfer estimator was not exercised");

    puts("locality/device placement tests: PASS");
    return 0;
}
