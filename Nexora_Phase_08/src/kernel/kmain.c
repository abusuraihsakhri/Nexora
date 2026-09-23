#include <kernel/printk.h>
#include <kernel/memory.h>
#include <kernel/pci.h>
#include <kernel/dma.h>
#include <ai/tensor.h>
#include <ai/work.h>
#include <ai/scheduler.h>
#include <ai/capability.h>
#include <ai/accelerator.h>
#include <ai/inference.h>
#include <drivers/nex_accel_sim.h>
#include <drivers/pci_accel.h>

static void print_tensor(const ai_tensor *t) {
    kputs("tensor ");
    kputs(t->name);
    kputs(" id=");
    kprint_u64(t->id);
    kputs(" dtype=");
    kputs(ai_dtype_name(t->dtype));
    kputs(" bytes=");
    kprint_u64(t->bytes);
    kputs(" location=");
    kputs(ai_location_name(t->location));
    kputs("\n");
}

static void print_work(const ai_work_node *node) {
    kputs("work node ");
    kprint_u64(node->id);
    kputs(" ");
    kputs(node->name);
    kputs(" op=");
    kputs(ai_op_name(node->op));
    kputs(" state=");
    kputs(ai_work_state_name(node->state));
    kputs(" priority=");
    kprint_u64(node->priority);
    kputs("\n");
}

static bool bytes_equal(const u8 *a, const u8 *b, usize size) {
    for (usize i = 0; i < size; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}

static void run_phase7_dma_demo(void) {
    kputs("\n[Phase 7 DMA + simulated accelerator]\n");

    dma_buffer src;
    dma_buffer dst;
    if (!dma_alloc(&src, 256, 64) || !dma_alloc(&dst, 256, 64)) {
        kputs("DMA allocation failed.\n");
        return;
    }

    u8 *src_bytes = (u8 *)src.virt;
    for (usize i = 0; i < src.size; ++i) {
        src_bytes[i] = (u8)(i ^ 0xA5u);
    }
    dma_sync_for_device(&src);

    ai_accel_device *device = ai_accel_find_for_mask(AI_DEVICE_NPU);
    if (!device) {
        kputs("No online simulated accelerator found.\n");
        return;
    }

    ai_accel_command command;
    command.command_id = 1;
    command.type = AI_ACCEL_CMD_COPY;
    command.status = AI_ACCEL_STATUS_EMPTY;
    command.work_id = 0;
    command.op = AI_OP_TRANSFER;
    command.src_phys = src.phys;
    command.dst_phys = dst.phys;
    command.bytes = src.size;
    command.flags = 0;

    bool submitted = ai_accel_submit(device, &command);
    bool completed = submitted && ai_accel_wait(device, &command, 1000);
    dma_sync_for_cpu(&dst);
    bool copied = completed && bytes_equal((const u8 *)src.virt, (const u8 *)dst.virt, src.size);

    kputs("DMA command status: ");
    kputs(ai_accel_status_name(command.status));
    kputs("\nDMA copy verification: ");
    kputs(copied ? "PASS\n" : "FAIL\n");
}

static void run_ai_graph_demo(void) {
    kputs("\n[Nexora AI work graph]\n");

    u64 input_shape[2]   = {1, 4096};
    u64 weight_shape[2]  = {4096, 4096};
    u64 hidden_shape[2]  = {1, 4096};

    ai_tensor *input = ai_tensor_create(
        "input",
        AI_DTYPE_F16,
        2,
        input_shape,
        AI_LOC_CPU_RAM,
        AI_TENSOR_EPHEMERAL
    );

    ai_tensor *weights = ai_tensor_create(
        "weights",
        AI_DTYPE_F16,
        2,
        weight_shape,
        AI_LOC_GPU_HBM,
        AI_TENSOR_PERSISTENT | AI_TENSOR_READONLY
    );

    ai_tensor *hidden = ai_tensor_create(
        "hidden",
        AI_DTYPE_F16,
        2,
        hidden_shape,
        AI_LOC_GPU_HBM,
        AI_TENSOR_EPHEMERAL
    );

    ai_tensor *output = ai_tensor_create(
        "output",
        AI_DTYPE_F16,
        2,
        hidden_shape,
        AI_LOC_GPU_HBM,
        AI_TENSOR_EPHEMERAL
    );

    print_tensor(input);
    print_tensor(weights);
    print_tensor(hidden);
    print_tensor(output);

    ai_work_graph graph;
    ai_work_graph_init(&graph);

    ai_work_node *matmul = ai_work_add(
        &graph,
        "projection",
        AI_OP_MATMUL,
        100,
        1000000,
        AI_DEVICE_GPU
    );
    ai_work_add_input(matmul, input);
    ai_work_add_input(matmul, weights);
    ai_work_add_output(matmul, hidden);

    ai_work_node *activation = ai_work_add(
        &graph,
        "activation",
        AI_OP_ACTIVATION,
        90,
        2000000,
        AI_DEVICE_GPU
    );
    ai_work_add_dependency(activation, matmul->id);
    ai_work_add_input(activation, hidden);
    ai_work_add_output(activation, output);

    ai_scheduler scheduler;
    ai_scheduler_init(&scheduler, &graph);

    print_work(matmul);
    print_work(activation);

    for (;;) {
        ai_work_node *selected = ai_scheduler_pick(&scheduler);
        if (!selected) {
            break;
        }

        kputs("scheduler selected node ");
        kprint_u64(selected->id);
        kputs("\n");
        ai_scheduler_mark_running(&scheduler, selected);

        ai_accel_device *device = ai_accel_find_for_mask(selected->device_mask);
        ai_accel_command command;
        bool submitted = device && ai_accel_submit_work(device, selected, &command);
        bool completed = submitted && ai_accel_wait(device, &command, 1000);

        kputs("device path result: ");
        kputs(submitted ? ai_accel_status_name(command.status) : "NO_BACKEND");
        kputs("\n");

        if (completed) {
            ai_scheduler_mark_done(&scheduler, selected);
        } else {
            selected->state = AI_WORK_FAILED;
        }
    }

    ai_capability agent_cap = ai_cap_create(
        42,
        AI_CAP_TENSOR_READ | AI_CAP_MODEL_USE | AI_CAP_GPU_USE
    );

    kputs("agent 42 GPU capability: ");
    kputs(ai_cap_has(&agent_cap, AI_CAP_GPU_USE) ? "yes\n" : "no\n");
}

static void run_phase7_device_discovery(void) {
    kputs("\n[Phase 7 device discovery]\n");
    pci_init();
    pci_enumerate();
    pci_print_summary();

    u32 inert = pci_accel_discover();
    kputs("PCI accelerator candidates registered (offline): ");
    kprint_u64(inert);
    kputs("\n");
}


static void run_phase8_inference_demo(void) {
    kputs("\n[Phase 8 inference orchestration]\n");

    ai_inference_init(
        768ull * 1024ull * 1024ull,
        64ull * 1024ull * 1024ull,
        90
    );

    ai_model *model = ai_model_register(
        "demo-llm",
        128ull * 1024ull * 1024ull,
        AI_DEVICE_GPU,
        true
    );
    if (!model || !ai_model_set_resident(model->id, AI_LOC_GPU_HBM)) {
        kputs("Model residency setup: FAIL\n");
        return;
    }

    ai_kv_state *kv = ai_kv_register(
        model->id,
        1,
        16ull * 1024ull * 1024ull,
        AI_LOC_GPU_HBM,
        true,
        1000
    );

    ai_infer_request *req = ai_infer_submit(
        model->id,
        1,
        128,
        64,
        1000,
        5000,
        2ull * 1024ull * 1024ull
    );

    kputs("model state: ");
    kputs(ai_model_state_name(model->state));
    kputs("\nKV residency: ");
    kputs(kv ? "READY\n" : "FAILED\n");
    kputs("request admission: ");
    kputs(req ? ai_admission_result_name(req->admission) : "NO_SLOT");
    kputs("\n");

    ai_infer_batch batch;
    if (req && req->state == AI_REQUEST_QUEUED &&
        ai_infer_form_batch(model->id, 4, 512, 1200, &batch)) {
        kputs("batch requests: ");
        kprint_u64(batch.request_count);
        kputs("\n");
        if (ai_infer_mark_running(&batch)) {
            for (u32 i = 0; i < batch.request_count; ++i) {
                (void)ai_infer_complete_request(batch.request_ids[i]);
            }
        }
    }

    ai_prefetch_plan plan;
    bool planned = ai_infer_plan_prefetch(
        44,
        AI_LOC_CPU_RAM,
        AI_LOC_GPU_HBM,
        4ull * 1024ull * 1024ull,
        6000,
        &plan
    );
    kputs("prefetch capacity decision: ");
    kputs(planned && plan.admitted ? "ADMIT\n" : "REJECT\n");

    kputs("inference headroom bytes: ");
    kprint_u64(ai_inference_headroom_bytes());
    kputs("\n");
}

void kmain(void) {
    console_init();

    kputs("Nexora AI-native kernel booted.\n");

    early_heap_init();
    kputs("Early heap initialized.\n");

    ai_tensor_system_init();
    ai_accel_system_init();
    kputs("AI runtime metadata initialized.\n");

    if (nex_accel_sim_init()) {
        kputs("Simulated accelerator registered.\n");
    } else {
        kputs("Simulated accelerator registration failed.\n");
    }

    run_phase7_device_discovery();
    run_phase7_dma_demo();
    run_phase8_inference_demo();
    run_ai_graph_demo();
    ai_accel_print_summary();

    kputs("early heap used: ");
    kprint_u64(early_heap_used());
    kputs(" / ");
    kprint_u64(early_heap_capacity());
    kputs(" bytes\n");

    kputs("\nNexora Phase 8 initialization complete.\n");
    kputs("Halting CPU.\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
