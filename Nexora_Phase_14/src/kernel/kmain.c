#include <kernel/printk.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/frame.h>
#include <kernel/slab.h>
#include <kernel/x86_64.h>
#include <kernel/idt.h>
#include <ai/backend_bridge.h>
#include <ai/tensor.h>
#include <ai/work.h>
#include <ai/scheduler.h>
#include <ai/capability.h>
#include <ai/integration.h>

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

static void print_phase14_report(const ai_phase14_report *report) {
    kputs("\n[Nexora Phase 14 validation]\n");
    kputs("status: ");
    kputs(report->passed ? "PASS\n" : "FAIL\n");
    kputs("assertions: ");
    kprint_u64(report->assertions_passed);
    kputs("/");
    kprint_u64(report->assertions);
    kputs("\n");
    kputs("stress rounds: ");
    kprint_u64(report->stress_rounds_completed);
    kputs("/");
    kprint_u64(AI_PHASE14_STRESS_ROUNDS);
    kputs("\n");
    kputs("graphs validated/completed: ");
    kprint_u64(report->graphs_validated);
    kputs("/");
    kprint_u64(report->graphs_completed);
    kputs("\n");
    kputs("deadlocks detected: ");
    kprint_u64(report->deadlocks_detected);
    kputs("\n");
    kputs("scheduler dispatches: ");
    kprint_u64(report->scheduler_dispatches);
    kputs("\n");
    kputs("benchmark scenarios: ");
    kprint_u64(report->benchmark.scenarios_completed);
    kputs("/");
    kprint_u64(report->benchmark.scenarios_run);
    kputs(" scans=");
    kprint_u64(report->benchmark.candidates_scanned);
    kputs("\n");
    kputs("heap growth: ");
    kprint_u64(report->heap_growth);
    kputs(" bytes\n");
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

    ai_graph_validation validation;
    if (!ai_work_graph_validate(&graph, &validation)) {
        panic("demo graph validation failed");
    }

    ai_scheduler scheduler;
    ai_scheduler_init(&scheduler, &graph);

    print_work(matmul);
    print_work(activation);

    ai_scheduler_run_report run;
    if (!ai_scheduler_run_to_completion(&scheduler, &run)) {
        panic("demo scheduler failed to complete graph");
    }

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

    kputs("Nexora / AIKernel x86_64 booted.\n");

    early_heap_init();
    kputs("Early heap initialized.\n");

    frame_init(0x2000000, 1024);
    kputs("Physical frame allocator initialized (1024 frames).\n");

    slab_init();
    kputs("Kernel slab caches initialized (ai_tensor & ai_work_node).\n");

    /* Wire Phase 5 privilege boundary & CPU tables (Milestone M3) */
    extern u8 stack_top[];
    nexora_x86_gdt_init((uintptr_t)stack_top);
    kputs("GDT and TSS loaded (Ring 0 / Ring 3 descriptors active).\n");

    idt_init();
    kputs("IDT loaded (256 vectors configured).\n");

    nexora_x86_syscall_init((uintptr_t)stack_top);
    kputs("Syscall MSRs & per-CPU GS initialized.\n");

    ai_backend_bridge_install();
    kputs("AI runtime backend bridge installed for userspace syscalls.\n");

    /* Milestone M3: Verify privilege boundary by deliberate int3 */
    u64 bp_before = idt_breakpoint_count();
    idt_test_breakpoint();
    if (idt_breakpoint_count() == bp_before + 1) {
        kputs("Privilege boundary verified: deliberate int3 caught by IDT (#BP).\n");
    } else {
        panic("IDT breakpoint verification failed");
    }

    ai_tensor_system_init();
    kputs("AI runtime metadata initialized.\n");

    ai_phase14_report phase14;
    if (!ai_phase14_run(&phase14)) {
        print_phase14_report(&phase14);
        panic("Phase 14 validation failed");
    }
    print_phase14_report(&phase14);

    ai_tensor_system_init();
    run_demo();

    kputs("\nNexora initialization complete.\n");
    kputs("Halting CPU.\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
