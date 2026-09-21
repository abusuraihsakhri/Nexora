#include <kernel/printk.h>
#include <kernel/memory.h>
#include <kernel/object.h>
#include <kernel/memory_object.h>
#include <kernel/panic.h>
#include <ai/tensor.h>
#include <ai/work.h>
#include <ai/scheduler.h>
#include <ai/capability.h>
#include <ai/device.h>
#include <ai/reclaim.h>
#include <ai/instrument.h>
#include <ai/debug.h>

static void run_object_selftest(void) {
    nx_object probe;

    if (!nx_object_register(
            &probe,
            NX_OBJECT_DEVICE,
            "object-selftest",
            NX_OBJECT_FLAG_KERNEL)) {
        panic("object self-test registration failed");
    }

    nx_handle_t stale_handle = probe.handle;

    if (nx_object_lookup(stale_handle, NX_OBJECT_DEVICE) != &probe) {
        panic("object self-test lookup failed");
    }

    if (nx_object_lookup(stale_handle, NX_OBJECT_TENSOR) != NULL) {
        panic("object self-test type check failed");
    }

    if (probe.owner_id != NX_OWNER_KERNEL) {
        panic("object self-test owner initialization failed");
    }

    if (!nx_object_retain(stale_handle) || nx_object_ref_count(&probe) != 2) {
        panic("object self-test retain failed");
    }
    if (!nx_object_release(stale_handle) || nx_object_ref_count(&probe) != 1) {
        panic("object self-test release failed");
    }
    if (!nx_object_pin(stale_handle) || nx_object_pin_count(&probe) != 1) {
        panic("object self-test pin failed");
    }
    if (nx_object_retire(&probe)) {
        panic("object self-test retired pinned object");
    }
    if (!nx_object_unpin(stale_handle)) {
        panic("object self-test unpin failed");
    }

    if (!nx_object_retire(&probe)) {
        panic("object self-test retirement failed");
    }

    if (nx_object_lookup(stale_handle, NX_OBJECT_DEVICE) != NULL) {
        panic("object self-test stale handle accepted");
    }

    kputs("Kernel object self-test passed.\n");
}


static void run_device_selftest(void) {
    ai_device *cpu = ai_device_default(AI_DEVICE_CPU);
    ai_device *gpu = ai_device_default(AI_DEVICE_GPU);
    ai_device *npu = ai_device_default(AI_DEVICE_NPU);
    if (cpu == NULL || gpu == NULL || npu == NULL || ai_device_count() != 3) {
        panic("device topology self-test failed");
    }

    u64 transfer_ns = 0;
    if (ai_device_estimate_transfer_ns(
            cpu->object.handle,
            gpu->object.handle,
            4ull * 1024ull * 1024ull,
            &transfer_ns) != AI_DEVICE_OK ||
        transfer_ns == 0) {
        panic("device transfer estimator self-test failed");
    }

    kputs("AI device/locality model self-test passed.\n");
}

static void run_memory_selftest(void) {
    nx_memory_desc desc;
    desc.name = "memory-selftest";
    desc.size_bytes = 96;
    desc.alignment = 64;
    desc.flags = NX_MEMORY_FLAG_ZERO_INIT;

    nx_memory *memory = NULL;
    if (nx_memory_try_create(&desc, &memory) != NX_MEMORY_OK || memory == NULL) {
        panic("memory self-test allocation failed");
    }

    if (memory->capacity_bytes != NX_MEMORY_PAGE_SIZE ||
        memory->alignment != NX_MEMORY_PAGE_SIZE) {
        panic("memory self-test page geometry failed");
    }

    if (nx_memory_lookup(memory->object.handle) != memory) {
        panic("memory self-test handle lookup failed");
    }

    u8 *bytes = (u8 *)nx_memory_ptr(memory, 0, desc.size_bytes);
    if (bytes == NULL) {
        panic("memory self-test mapping failed");
    }

    for (u64 i = 0; i < desc.size_bytes; ++i) {
        if (bytes[i] != 0) {
            panic("memory self-test zeroing failed");
        }
    }

    if (nx_memory_ptr(memory, memory->capacity_bytes, 1) != NULL) {
        panic("memory self-test bounds failed");
    }

    nx_handle_t memory_handle = memory->object.handle;
    if (!nx_memory_release(memory_handle) ||
        nx_memory_lookup(memory_handle) != NULL) {
        panic("memory self-test final release failed");
    }

    kputs("Memory ownership/refcount self-test passed.\n");
}

static void print_tensor(const ai_tensor *t) {
    kputs("tensor ");
    kputs(t->object.name);
    kputs(" id=");
    kprint_u64(t->object.id);
    kputs(" owner=");
    kprint_u64(t->object.owner_id);
    kputs(" refs=");
    kprint_u64(t->object.strong_refs);
    kputs(" class=");
    kputs(ai_tensor_class_name(t->tensor_class));
    kputs(" dtype=");
    kputs(ai_dtype_name(t->dtype));
    kputs(" rank=");
    kprint_u64(t->ndim);
    kputs(" elements=");
    kprint_u64(t->element_count);
    kputs(" logical_bytes=");
    kprint_u64(t->logical_bytes);
    kputs(" span_bytes=");
    kprint_u64(t->storage_span_bytes);
    kputs(" layout=");
    kputs(ai_tensor_layout_name(t->layout));
    kputs(" location=");
    kputs(ai_location_name(t->location));
    kputs(" lifetime=");
    kputs(ai_tensor_lifetime_name(t->lifetime));
    kputs(" residency=");
    kputs(ai_tensor_residency_name(t->residency));
    kputs(" backed=");
    kputs(ai_tensor_is_backed(t) ? "yes" : "no");
    kputs(" preferred_device=");
    ai_device *preferred = ai_device_lookup(t->preferred_device);
    kputs(preferred != NULL ? preferred->object.name : "none");
    kputs(" resident_device=");
    ai_device *resident = ai_device_lookup(t->resident_device);
    kputs(resident != NULL ? resident->object.name : "none");

    nx_memory *backing = ai_tensor_backing(t);
    if (backing != NULL) {
        kputs(" backend=");
        kputs(nx_memory_backend_name(backing->backend));
        kputs(" capacity=");
        kprint_u64(backing->capacity_bytes);
        kputs(" mem_refs=");
        kprint_u64(backing->object.strong_refs);
        kputs(" mem_pins=");
        kprint_u64(backing->object.pin_count);
        if (backing->flags & NX_MEMORY_FLAG_EMULATED_DEVICE) {
            kputs(" emulated-device=yes");
        }
    }
    kputs("\n");

    kputs("  shape=[");
    for (u32 i = 0; i < t->ndim; ++i) {
        if (i != 0) kputs(",");
        kprint_u64(t->shape[i]);
    }
    kputs("] stride_bytes=[");
    for (u32 i = 0; i < t->ndim; ++i) {
        if (i != 0) kputs(",");
        kprint_u64(t->stride_bytes[i]);
    }
    kputs("]\n");
}

static void run_tensor_selftest(void) {
    u64 overflow_shape[2] = {~(u64)0, 2};
    ai_tensor_desc overflow_desc;
    overflow_desc.name = "overflow-probe";
    overflow_desc.dtype = AI_DTYPE_F32;
    overflow_desc.tensor_class = AI_TENSOR_CLASS_SCRATCH;
    overflow_desc.ndim = 2;
    overflow_desc.shape = overflow_shape;
    overflow_desc.stride_bytes = NULL;
    overflow_desc.location = AI_LOC_CPU_RAM;
    overflow_desc.lifetime = AI_TENSOR_LIFETIME_TEMPORARY;
    overflow_desc.flags = AI_TENSOR_EPHEMERAL;

    ai_tensor *probe = NULL;
    if (ai_tensor_try_create(&overflow_desc, &probe) != AI_TENSOR_ERR_SHAPE_OVERFLOW) {
        panic("tensor overflow self-test failed");
    }
    if (probe != NULL) {
        panic("tensor overflow self-test allocated object");
    }

    u64 strided_shape[2] = {2, 3};
    u64 strided_bytes[2] = {16, 4};
    ai_tensor_desc strided_desc;
    strided_desc.name = "strided-selftest";
    strided_desc.dtype = AI_DTYPE_F32;
    strided_desc.tensor_class = AI_TENSOR_CLASS_SCRATCH;
    strided_desc.ndim = 2;
    strided_desc.shape = strided_shape;
    strided_desc.stride_bytes = strided_bytes;
    strided_desc.location = AI_LOC_CPU_RAM;
    strided_desc.lifetime = AI_TENSOR_LIFETIME_TEMPORARY;
    strided_desc.flags = AI_TENSOR_EPHEMERAL | AI_TENSOR_VIEW;

    if (ai_tensor_try_create(&strided_desc, &probe) != AI_TENSOR_OK || probe == NULL) {
        panic("tensor strided self-test creation failed");
    }
    if (probe->layout != AI_TENSOR_LAYOUT_STRIDED ||
        probe->logical_bytes != 24 ||
        probe->storage_span_bytes != 28) {
        panic("tensor strided self-test metadata failed");
    }

    u64 backed_shape[1] = {32};
    ai_tensor *backed = ai_tensor_create(
        "backing-selftest",
        AI_DTYPE_F32,
        AI_TENSOR_CLASS_ACTIVATION,
        1,
        backed_shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL | AI_TENSOR_ZERO_INIT
    );

    if (ai_tensor_allocate_backing(backed, 64) != AI_TENSOR_OK ||
        !ai_tensor_is_backed(backed) || ai_tensor_data(backed) == NULL) {
        panic("tensor backing self-test failed");
    }

    nx_handle_t backing_handle = backed->backing_memory;
    if (!ai_tensor_release(backed) || nx_memory_lookup(backing_handle) != NULL) {
        panic("tensor backing ownership self-test failed");
    }
    if (!ai_tensor_release(probe)) {
        panic("tensor destruction self-test failed");
    }

    kputs("Tensor metadata/backing/refcount self-test passed.\n");
}

static void run_lifetime_selftest(void) {
    u64 shape[1] = {64};
    ai_tensor *temporary = ai_tensor_create_with_lifetime(
        "lifetime-selftest",
        AI_DTYPE_F32,
        AI_TENSOR_CLASS_ACTIVATION,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_LIFETIME_TEMPORARY,
        AI_TENSOR_ZERO_INIT
    );

    if (ai_tensor_allocate_backing(temporary, NX_MEMORY_PAGE_SIZE) != AI_TENSOR_OK) {
        panic("lifetime self-test backing failed");
    }
    nx_handle_t memory_handle = temporary->backing_memory;
    if (ai_tensor_lifetime_reclaim(temporary, AI_RECLAIM_FINAL_CONSUMER) != AI_LIFETIME_OK ||
        temporary->residency != AI_TENSOR_RESIDENCY_RECLAIMED ||
        ai_tensor_is_backed(temporary) || nx_memory_lookup(memory_handle) != NULL) {
        panic("lifetime self-test reclaim failed");
    }
    if (!ai_tensor_release(temporary)) {
        panic("lifetime self-test tensor release failed");
    }

    kputs("Tensor lifetime-engine self-test passed.\n");
}


static void run_reclaim_selftest(void) {
    u64 shape[1] = {64};
    ai_tensor *cached = ai_tensor_create_with_lifetime(
        "reclaim-selftest-cache",
        AI_DTYPE_F32,
        AI_TENSOR_CLASS_KV_CACHE,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_LIFETIME_CACHED,
        AI_TENSOR_ZERO_INIT
    );

    if (ai_tensor_allocate_backing(cached, NX_MEMORY_PAGE_SIZE) != AI_TENSOR_OK) {
        panic("reclaim self-test backing failed");
    }

    ai_reclaim_report report;
    if (ai_reclaim_evict_cache(NX_MEMORY_PAGE_SIZE, &report) != AI_RECLAIM_OK ||
        !report.target_met ||
        report.resident_bytes_reclaimed < NX_MEMORY_PAGE_SIZE ||
        cached->residency != AI_TENSOR_RESIDENCY_RECLAIMED) {
        panic("automatic reclaim self-test failed");
    }

    if (!ai_tensor_release(cached)) {
        panic("reclaim self-test tensor release failed");
    }

    kputs("Automatic reclamation self-test passed.\n");
}

static void run_graph_selftest(void) {
    u64 shape[1] = {16};
    ai_tensor *source = ai_tensor_create_with_lifetime(
        "graph-source-selftest",
        AI_DTYPE_F32,
        AI_TENSOR_CLASS_INPUT,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_LIFETIME_TEMPORARY,
        AI_TENSOR_ZERO_INIT
    );
    ai_tensor *intermediate = ai_tensor_create_with_lifetime(
        "graph-intermediate-selftest",
        AI_DTYPE_F32,
        AI_TENSOR_CLASS_ACTIVATION,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_LIFETIME_TEMPORARY,
        AI_TENSOR_ZERO_INIT
    );

    if (ai_tensor_allocate_backing(source, NX_MEMORY_PAGE_SIZE) != AI_TENSOR_OK ||
        ai_tensor_allocate_backing(intermediate, NX_MEMORY_PAGE_SIZE) != AI_TENSOR_OK) {
        panic("graph self-test backing failed");
    }

    ai_work_graph graph;
    ai_work_graph_init(&graph);
    ai_work_node *producer = ai_work_add(
        &graph, "graph-producer-selftest", AI_OP_CUSTOM, 10, 100, AI_DEVICE_CPU
    );
    ai_work_node *consumer = ai_work_add(
        &graph, "graph-consumer-selftest", AI_OP_CUSTOM, 9, 200, AI_DEVICE_CPU
    );
    ai_work_add_input(producer, source);
    ai_work_add_output(producer, intermediate);
    ai_work_add_input(consumer, intermediate);

    ai_work_refresh_states(&graph);
    if (producer->state != AI_WORK_READY || consumer->state != AI_WORK_PENDING ||
        ai_tensor_producer_work(intermediate) != producer->object.handle ||
        ai_tensor_consumers_remaining(intermediate) != 1) {
        panic("graph self-test topology failed");
    }

    producer->state = AI_WORK_RUNNING;
    if (ai_work_complete(producer) != AI_WORK_OK ||
        source->residency != AI_TENSOR_RESIDENCY_RECLAIMED) {
        panic("graph self-test producer completion failed");
    }

    ai_work_refresh_states(&graph);
    if (consumer->state != AI_WORK_READY) {
        panic("graph self-test data dependency failed");
    }
    consumer->state = AI_WORK_RUNNING;
    if (ai_work_complete(consumer) != AI_WORK_OK ||
        ai_tensor_consumers_remaining(intermediate) != 0 ||
        intermediate->residency != AI_TENSOR_RESIDENCY_RECLAIMED) {
        panic("graph self-test final-consumer reclaim failed");
    }

    ai_work_graph_destroy(&graph);
    if (!ai_tensor_release(source) || !ai_tensor_release(intermediate)) {
        panic("graph self-test cleanup failed");
    }

    kputs("Producer/consumer graph self-test passed.\n");
}


static void run_work_object_selftest(void) {
    ai_work_graph graph;
    ai_work_graph_init(&graph);

    ai_work_desc latency_desc;
    latency_desc.name = "work-profile-selftest";
    latency_desc.op = AI_OP_MATMUL;
    latency_desc.work_class = AI_WORK_CLASS_COMPUTE;
    latency_desc.qos = AI_WORK_QOS_LATENCY;
    latency_desc.priority = 42;
    latency_desc.deadline_ns = AI_WORK_NO_DEADLINE;
    latency_desc.device_mask = AI_DEVICE_CPU | AI_DEVICE_GPU;
    latency_desc.preferred_device = AI_DEVICE_INVALID;
    latency_desc.estimated_ops = 8192;
    latency_desc.estimated_read_bytes = 0;
    latency_desc.estimated_write_bytes = 0;
    latency_desc.estimated_scratch_bytes = 4096;
    latency_desc.estimated_duration_ns = 500;
    latency_desc.batch_id = 7;
    latency_desc.batch_size = 2;
    latency_desc.flags = AI_WORK_FLAG_AUTO_IO_BYTES |
                         AI_WORK_FLAG_PREEMPTIBLE |
                         AI_WORK_FLAG_BATCHABLE;

    ai_work_node *latency = NULL;
    if (ai_work_try_create(&graph, &latency_desc, &latency) != AI_WORK_OK ||
        latency == NULL || !ai_work_is_profiled(latency) ||
        ai_work_has_deadline(latency) || latency->batch_size != 2) {
        panic("work-object profile self-test failed");
    }

    ai_work_desc deadline_desc = latency_desc;
    deadline_desc.name = "work-deadline-selftest";
    deadline_desc.qos = AI_WORK_QOS_BACKGROUND;
    deadline_desc.deadline_ns = 1000;
    deadline_desc.batch_id = AI_WORK_NO_BATCH;
    deadline_desc.batch_size = 1;
    deadline_desc.flags = AI_WORK_FLAG_AUTO_IO_BYTES;

    ai_work_node *deadline = ai_work_create(&graph, &deadline_desc);

    ai_scheduler scheduler;
    ai_scheduler_init(&scheduler, &graph);
    if (ai_scheduler_pick(&scheduler) != deadline) {
        panic("work-object scheduler metadata self-test failed");
    }

    ai_work_desc invalid_desc = latency_desc;
    invalid_desc.name = "work-invalid-estimate-selftest";
    invalid_desc.estimated_read_bytes = 1;
    ai_work_node *invalid_node = NULL;
    if (ai_work_try_create(&graph, &invalid_desc, &invalid_node) !=
            AI_WORK_ERR_INVALID_ESTIMATE ||
        invalid_node != NULL) {
        panic("work-object validation self-test failed");
    }

    ai_work_graph_destroy(&graph);
    kputs("AI work-object redesign self-test passed.\n");
}

static void run_placement_selftest(void) {
    ai_device *cpu = ai_device_default(AI_DEVICE_CPU);
    ai_device *gpu = ai_device_default(AI_DEVICE_GPU);
    if (cpu == NULL || gpu == NULL) {
        panic("placement self-test devices missing");
    }

    u64 shape[1] = {1024};
    ai_tensor *input = ai_tensor_create_with_lifetime(
        "placement-input",
        AI_DTYPE_F32,
        AI_TENSOR_CLASS_INPUT,
        1,
        shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_LIFETIME_TEMPORARY,
        0
    );
    ai_tensor *output = ai_tensor_create_with_lifetime(
        "placement-output",
        AI_DTYPE_F32,
        AI_TENSOR_CLASS_OUTPUT,
        1,
        shape,
        AI_LOC_GPU_HBM,
        AI_TENSOR_LIFETIME_TEMPORARY,
        0
    );
    if (ai_tensor_allocate_backing(input, NX_MEMORY_PAGE_SIZE) != AI_TENSOR_OK ||
        ai_tensor_allocate_backing(output, NX_MEMORY_PAGE_SIZE) != AI_TENSOR_OK) {
        panic("placement self-test backing failed");
    }

    ai_work_graph graph;
    ai_work_graph_init(&graph);
    ai_work_desc desc;
    desc.name = "placement-selftest";
    desc.op = AI_OP_CUSTOM;
    desc.work_class = AI_WORK_CLASS_COMPUTE;
    desc.qos = AI_WORK_QOS_DEFAULT;
    desc.priority = 1;
    desc.deadline_ns = AI_WORK_NO_DEADLINE;
    desc.device_mask = AI_DEVICE_CPU | AI_DEVICE_GPU;
    desc.preferred_device = AI_DEVICE_INVALID;
    desc.estimated_ops = 1000000000ull;
    desc.estimated_read_bytes = 0;
    desc.estimated_write_bytes = 0;
    desc.estimated_scratch_bytes = 0;
    desc.estimated_duration_ns = 0;
    desc.batch_id = AI_WORK_NO_BATCH;
    desc.batch_size = 1;
    desc.flags = AI_WORK_FLAG_AUTO_IO_BYTES;

    ai_work_node *node = ai_work_create(&graph, &desc);
    ai_work_add_input(node, input);
    ai_work_add_output(node, output);
    ai_work_refresh_states(&graph);

    ai_dispatch_choice choice;
    if (ai_placement_choose(node, &choice) != AI_PLACEMENT_OK ||
        choice.device_id != gpu->object.handle ||
        choice.input_transfers != 1) {
        panic("placement cost self-test failed");
    }

    ai_scheduler scheduler;
    ai_scheduler_init(&scheduler, &graph);
    if (ai_scheduler_pick_dispatch(&scheduler, &choice) != AI_PLACEMENT_OK ||
        choice.work != node ||
        ai_scheduler_mark_running_on(&scheduler, node, choice.device_id) != AI_WORK_OK ||
        ai_scheduler_complete(&scheduler, node) != AI_WORK_OK) {
        panic("placement dispatch self-test failed");
    }
    if (output->resident_device != gpu->object.handle) {
        panic("placement output residency self-test failed");
    }

    ai_work_graph_destroy(&graph);
    if (!ai_tensor_release(input) || !ai_tensor_release(output)) {
        panic("placement self-test cleanup failed");
    }
    kputs("Locality/device placement self-test passed.\n");
}

static void print_work(const ai_work_node *node) {
    kputs("work node ");
    kprint_u64(node->object.id);
    kputs(" ");
    kputs(node->object.name);
    kputs(" op=");
    kputs(ai_op_name(node->op));
    kputs(" class=");
    kputs(ai_work_class_name(node->work_class));
    kputs(" qos=");
    kputs(ai_work_qos_name(node->qos));
    kputs(" state=");
    kputs(ai_work_state_name(node->state));
    kputs(" priority=");
    kprint_u64(node->priority);
    kputs(" ops=");
    kprint_u64(node->estimated_ops);
    kputs(" read=");
    kprint_u64(node->estimated_read_bytes);
    kputs(" write=");
    kprint_u64(node->estimated_write_bytes);
    kputs(" scratch=");
    kprint_u64(node->estimated_scratch_bytes);
    kputs(" duration_ns=");
    kprint_u64(node->estimated_duration_ns);
    kputs(" batch=");
    kprint_u64(node->batch_id);
    kputs("/");
    kprint_u64(node->batch_size);
    kputs(" preferred_device=");
    ai_device *preferred = ai_device_lookup(node->preferred_device);
    kputs(preferred != NULL ? preferred->object.name : "none");
    kputs(" selected_device=");
    ai_device *selected = ai_device_lookup(node->selected_device);
    kputs(selected != NULL ? selected->object.name : "none");
    kputs("\n");
}

static void run_demo(void) {
    kputs("\n[Nexora Phase 3 instrumentation demo]\n");
    ai_trace_reset();
    ai_trace_set_enabled(true);
    ai_trace_memory_sample(0);

    u64 input_shape[2]   = {1, 256};
    u64 weight_shape[2]  = {256, 256};
    u64 hidden_shape[2]  = {1, 256};

    ai_tensor *input = ai_tensor_create(
        "input",
        AI_DTYPE_F16,
        AI_TENSOR_CLASS_INPUT,
        2,
        input_shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL
    );

    ai_tensor *weights = ai_tensor_create(
        "weights",
        AI_DTYPE_F16,
        AI_TENSOR_CLASS_WEIGHT,
        2,
        weight_shape,
        AI_LOC_GPU_HBM,
        AI_TENSOR_PERSISTENT | AI_TENSOR_READONLY
    );

    ai_tensor *hidden = ai_tensor_create(
        "hidden",
        AI_DTYPE_F16,
        AI_TENSOR_CLASS_ACTIVATION,
        2,
        hidden_shape,
        AI_LOC_GPU_HBM,
        AI_TENSOR_EPHEMERAL
    );

    ai_tensor *output = ai_tensor_create(
        "output",
        AI_DTYPE_F16,
        AI_TENSOR_CLASS_OUTPUT,
        2,
        hidden_shape,
        AI_LOC_GPU_HBM,
        AI_TENSOR_EPHEMERAL
    );

    /* Inputs/weights start resident. Intermediate/output storage is allocated lazily. */
    if (ai_tensor_allocate_backing(input, NX_MEMORY_PAGE_SIZE) != AI_TENSOR_OK ||
        ai_tensor_allocate_backing(weights, NX_MEMORY_PAGE_SIZE) != AI_TENSOR_OK) {
        panic("demo tensor backing allocation failed");
    }
    ai_trace_memory_sample(1);

    print_tensor(input);
    print_tensor(weights);
    print_tensor(hidden);
    print_tensor(output);

    ai_work_graph graph;
    ai_work_graph_init(&graph);

    ai_work_desc matmul_desc;
    matmul_desc.name = "projection";
    matmul_desc.op = AI_OP_MATMUL;
    matmul_desc.work_class = AI_WORK_CLASS_COMPUTE;
    matmul_desc.qos = AI_WORK_QOS_LATENCY;
    matmul_desc.priority = 100;
    matmul_desc.deadline_ns = 1000000;
    matmul_desc.device_mask = AI_DEVICE_GPU;
    matmul_desc.preferred_device = AI_DEVICE_INVALID;
    matmul_desc.estimated_ops = 131072;
    matmul_desc.estimated_read_bytes = 0;
    matmul_desc.estimated_write_bytes = 0;
    matmul_desc.estimated_scratch_bytes = 4096;
    matmul_desc.estimated_duration_ns = 150000;
    matmul_desc.batch_id = 1;
    matmul_desc.batch_size = 1;
    matmul_desc.flags = AI_WORK_FLAG_AUTO_IO_BYTES |
                        AI_WORK_FLAG_PREEMPTIBLE |
                        AI_WORK_FLAG_DETERMINISTIC;
    ai_work_node *matmul = ai_work_create(&graph, &matmul_desc);
    ai_work_add_input(matmul, input);
    ai_work_add_input(matmul, weights);
    ai_work_add_output(matmul, hidden);

    ai_work_desc activation_desc;
    activation_desc.name = "activation";
    activation_desc.op = AI_OP_ACTIVATION;
    activation_desc.work_class = AI_WORK_CLASS_COMPUTE;
    activation_desc.qos = AI_WORK_QOS_LATENCY;
    activation_desc.priority = 90;
    activation_desc.deadline_ns = 2000000;
    activation_desc.device_mask = AI_DEVICE_GPU;
    activation_desc.preferred_device = AI_DEVICE_INVALID;
    activation_desc.estimated_ops = 256;
    activation_desc.estimated_read_bytes = 0;
    activation_desc.estimated_write_bytes = 0;
    activation_desc.estimated_scratch_bytes = 0;
    activation_desc.estimated_duration_ns = 10000;
    activation_desc.batch_id = 1;
    activation_desc.batch_size = 1;
    activation_desc.flags = AI_WORK_FLAG_AUTO_IO_BYTES |
                            AI_WORK_FLAG_PREEMPTIBLE |
                            AI_WORK_FLAG_DETERMINISTIC;
    ai_work_node *activation = ai_work_create(&graph, &activation_desc);
    ai_work_add_input(activation, hidden);
    ai_work_add_output(activation, output);

    ai_scheduler scheduler;
    ai_scheduler_init(&scheduler, &graph);

    print_work(matmul);
    print_work(activation);

    ai_dispatch_choice dispatch;
    ai_work_node *selected = NULL;
    if (ai_scheduler_pick_dispatch(&scheduler, &dispatch) == AI_PLACEMENT_OK) {
        selected = dispatch.work;
        ai_device *device = ai_device_lookup(dispatch.device_id);
        kputs("scheduler selected node ");
        kprint_u64(selected ? selected->object.id : 0);
        kputs(" device=");
        kputs(device != NULL ? device->object.name : "none");
        kputs(" transfer_ns=");
        kprint_u64(dispatch.transfer_ns);
        kputs(" execution_ns=");
        kprint_u64(dispatch.execution_ns);
        kputs(" total_ns=");
        kprint_u64(dispatch.total_ns);
        kputs("\n");
        for (u32 i = 0; selected != NULL && i < selected->output_count; ++i) {
            if (!ai_tensor_is_backed(selected->outputs[i]) &&
                ai_tensor_allocate_backing(selected->outputs[i], NX_MEMORY_PAGE_SIZE) != AI_TENSOR_OK) {
                panic("demo lazy output allocation failed");
            }
        }
        ai_trace_memory_sample(2);
        (void)ai_scheduler_mark_running_on(&scheduler, selected, dispatch.device_id);
        ai_scheduler_mark_done(&scheduler, selected);
        ai_trace_memory_sample(3);
    }

    kputs("hidden consumers remaining: ");
    kprint_u64(ai_tensor_consumers_remaining(hidden));
    kputs(" residency=");
    kputs(ai_tensor_residency_name(hidden->residency));
    kputs("\n");

    const ai_work_graph_stats *graph_stats = ai_work_graph_get_stats(&graph);
    kputs("graph final-consumer events: ");
    kprint_u64(graph_stats->final_consumer_events);
    kputs(" auto-reclaimed=");
    kprint_u64(graph_stats->automatic_reclaim_successes);
    kputs("\n");

    if (ai_scheduler_pick_dispatch(&scheduler, &dispatch) == AI_PLACEMENT_OK) {
        selected = dispatch.work;
        ai_device *device = ai_device_lookup(dispatch.device_id);
        kputs("scheduler selected node ");
        kprint_u64(selected ? selected->object.id : 0);
        kputs(" device=");
        kputs(device != NULL ? device->object.name : "none");
        kputs(" transfer_ns=");
        kprint_u64(dispatch.transfer_ns);
        kputs(" execution_ns=");
        kprint_u64(dispatch.execution_ns);
        kputs(" total_ns=");
        kprint_u64(dispatch.total_ns);
        kputs("\n");
        for (u32 i = 0; selected != NULL && i < selected->output_count; ++i) {
            if (!ai_tensor_is_backed(selected->outputs[i]) &&
                ai_tensor_allocate_backing(selected->outputs[i], NX_MEMORY_PAGE_SIZE) != AI_TENSOR_OK) {
                panic("demo lazy output allocation failed");
            }
        }
        ai_trace_memory_sample(4);
        (void)ai_scheduler_mark_running_on(&scheduler, selected, dispatch.device_id);
        ai_scheduler_mark_done(&scheduler, selected);
        ai_trace_memory_sample(5);
    }

    kputs("hidden after final consumer: remaining=");
    kprint_u64(ai_tensor_consumers_remaining(hidden));
    kputs(" residency=");
    kputs(ai_tensor_residency_name(hidden->residency));
    kputs("\n");

    graph_stats = ai_work_graph_get_stats(&graph);
    kputs("graph final-consumer events: ");
    kprint_u64(graph_stats->final_consumer_events);
    kputs(" auto-reclaimed=");
    kprint_u64(graph_stats->automatic_reclaim_successes);
    kputs("\n");

    ai_capability agent_cap = ai_cap_create(
        42,
        AI_CAP_TENSOR_READ | AI_CAP_MODEL_USE | AI_CAP_GPU_USE
    );

    kputs("agent 42 GPU capability: ");
    kputs(ai_cap_has(&agent_cap, AI_CAP_GPU_USE) ? "yes\n" : "no\n");

    kputs("kernel objects live: ");
    kprint_u64(nx_object_count());
    kputs("\n");

    nx_object *lookup = nx_object_lookup(hidden->object.handle, NX_OBJECT_TENSOR);
    kputs("tensor handle lookup: ");
    kputs(lookup == &hidden->object ? "ok\n" : "failed\n");

    const ai_tensor_stats *tensor_stats = ai_tensor_get_stats();
    kputs("tensor logical bytes tracked: ");
    kprint_u64(tensor_stats->logical_bytes);
    kputs(" peak=");
    kprint_u64(tensor_stats->peak_logical_bytes);
    kputs("\n");

    const nx_memory_stats *memory_stats = nx_memory_get_stats();
    kputs("memory resident bytes tracked: ");
    kprint_u64(memory_stats->resident_bytes);
    kputs(" requested=");
    kprint_u64(memory_stats->requested_bytes);
    kputs(" peak=");
    kprint_u64(memory_stats->peak_resident_bytes);
    kputs("\n");

    kputs("early heap used: ");
    kprint_u64(early_heap_used());
    kputs(" / ");
    kprint_u64(early_heap_capacity());
    kputs(" bytes\n");

    ai_debug_dump_graph(&graph);
    ai_debug_dump_tensors();
    ai_debug_dump_memory_summary();
    ai_debug_dump_trace(48);

    ai_work_graph_destroy(&graph);
    if (!ai_tensor_release(input) || !ai_tensor_release(weights) ||
        !ai_tensor_release(hidden) || !ai_tensor_release(output)) {
        panic("demo cleanup failed");
    }
    ai_trace_set_enabled(false);
}

void kmain(void) {
    console_init();

    kputs("AIKernel x86_64 booted.\n");

    early_heap_init();
    kputs("Early heap initialized.\n");

    nx_object_system_init();
    kputs("Kernel object registry initialized.\n");
    run_object_selftest();

    ai_device_system_init();
    kputs("AI device/locality model initialized.\n");
    run_device_selftest();

    nx_memory_system_init();
    ai_instrument_system_init();
    ai_trace_set_enabled(false);
    kputs("Kernel memory-object system initialized.\n");
    kputs("AI instrumentation initialized.\n");
    run_memory_selftest();

    ai_tensor_system_init();
    ai_reclaim_system_init();
    kputs("AI runtime metadata initialized.\n");
    kputs("Automatic reclamation manager initialized.\n");
    run_tensor_selftest();
    run_lifetime_selftest();
    run_reclaim_selftest();
    run_graph_selftest();
    run_work_object_selftest();
    run_placement_selftest();

    run_demo();

    kputs("\nAIKernel initialization complete.\n");
    kputs("Halting CPU.\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
