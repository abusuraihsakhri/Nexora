#include <kernel/printk.h>
#include <kernel/memory.h>
#include <ai/tensor.h>
#include <ai/work.h>
#include <ai/scheduler.h>
#include <ai/capability.h>
#include <ai/policy_demo.h>

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

static void run_demo(void) {
    kputs("\n[AIKernel demo]\n");

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

    ai_work_node *selected = ai_scheduler_pick(&scheduler);
    kputs("scheduler selected node ");
    kprint_u64(selected ? selected->id : 0);
    kputs("\n");

    ai_scheduler_mark_running(&scheduler, selected);
    ai_scheduler_mark_done(&scheduler, selected);

    selected = ai_scheduler_pick(&scheduler);
    kputs("scheduler selected node ");
    kprint_u64(selected ? selected->id : 0);
    kputs("\n");

    ai_scheduler_mark_running(&scheduler, selected);
    ai_scheduler_mark_done(&scheduler, selected);

    ai_capability agent_cap = ai_cap_create(
        42,
        AI_CAP_TENSOR_READ | AI_CAP_MODEL_USE | AI_CAP_GPU_USE
    );

    kputs("agent 42 GPU capability: ");
    kputs(ai_cap_has(&agent_cap, AI_CAP_GPU_USE) ? "yes\n" : "no\n");

    kputs("early heap used: ");
    kprint_u64(early_heap_used());
    kputs(" / ");
    kprint_u64(early_heap_capacity());
    kputs(" bytes\n");
}

void kmain(void) {
    console_init();

    kputs("AIKernel x86_64 booted.\n");

    early_heap_init();
    kputs("Early heap initialized.\n");

    ai_tensor_system_init();
    kputs("AI runtime metadata initialized.\n");

    run_demo();
    ai_policy_run_phase11_demo();

    kputs("\nAIKernel initialization complete.\n");
    kputs("Halting CPU.\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
