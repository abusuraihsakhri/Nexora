#include <stdio.h>
#include <ai/benchmark.h>
#include <ai/capability.h>
#include <ai/integration.h>
#include <ai/scheduler.h>
#include <ai/tensor.h>
#include <ai/work.h>
#include <kernel/memory.h>
#include <kernel/frame.h>
#include <kernel/slab.h>
#include <kernel/x86_64.h>
#include <kernel/idt.h>
#include <kernel/demo_elf.h>
#include <ai/backend_bridge.h>
#include <nexora/elf64.h>
#include <nexora/syscall.h>
#include <nexora/process.h>
#include <nexora/uaccess.h>

static unsigned tests_run = 0;
static unsigned tests_failed = 0;

#define CHECK(condition) do { \
    tests_run++; \
    if (!(condition)) { \
        tests_failed++; \
        printf("not ok %u - %s:%d: %s\n", tests_run, __FILE__, __LINE__, #condition); \
    } else { \
        printf("ok %u - %s\n", tests_run, #condition); \
    } \
} while (0)

static void reset_runtime(void) {
    early_heap_init();
    ai_tensor_system_init();
}

static void test_tensor_accounting(void) {
    reset_runtime();
    u64 shape[2] = {4, 8};
    ai_tensor *a = ai_tensor_create(
        "a", AI_DTYPE_F16, 2, shape, AI_LOC_CPU_RAM, AI_TENSOR_EPHEMERAL
    );
    ai_tensor *b = ai_tensor_create(
        "b", AI_DTYPE_F32, 2, shape, AI_LOC_GPU_HBM, AI_TENSOR_PERSISTENT
    );

    CHECK(ai_tensor_validate(a));
    CHECK(ai_tensor_validate(b));
    CHECK(a->bytes == 64);
    CHECK(b->bytes == 128);
    CHECK(ai_tensor_total_bytes() == 192);
    CHECK(ai_tensor_bytes_at_location(AI_LOC_CPU_RAM) == 64);
    CHECK(ai_tensor_bytes_at_location(AI_LOC_GPU_HBM) == 128);
}

static void test_graph_validation(void) {
    reset_runtime();
    ai_work_graph graph;
    ai_work_graph_init(&graph);
    ai_work_node *a = ai_work_add(&graph, "a", AI_OP_NOOP, 1, 100, AI_DEVICE_CPU);
    ai_work_node *b = ai_work_add(&graph, "b", AI_OP_NOOP, 1, 200, AI_DEVICE_CPU);
    ai_work_add_dependency(b, a->id);

    ai_graph_validation validation;
    CHECK(ai_work_graph_validate(&graph, &validation));
    CHECK(validation.valid);
    CHECK(validation.node_count == 2);
    CHECK(validation.edge_count == 1);

    ai_work_graph missing;
    ai_work_graph_init(&missing);
    ai_work_node *m = ai_work_add(&missing, "missing", AI_OP_NOOP, 1, 1, AI_DEVICE_CPU);
    ai_work_add_dependency(m, 999);
    CHECK(!ai_work_graph_validate(&missing, &validation));
    CHECK(validation.missing_dependencies == 1);

    ai_work_graph cycle;
    ai_work_graph_init(&cycle);
    ai_work_node *c1 = ai_work_add(&cycle, "c1", AI_OP_NOOP, 1, 1, AI_DEVICE_CPU);
    ai_work_node *c2 = ai_work_add(&cycle, "c2", AI_OP_NOOP, 1, 1, AI_DEVICE_CPU);
    ai_work_add_dependency(c1, c2->id);
    ai_work_add_dependency(c2, c1->id);
    CHECK(!ai_work_graph_validate(&cycle, &validation));
    CHECK(validation.cycle_nodes == 2);
}

static void test_scheduler(void) {
    reset_runtime();
    ai_work_graph graph;
    ai_work_graph_init(&graph);
    ai_work_node *late = ai_work_add(&graph, "late", AI_OP_NOOP, 50, 5000, AI_DEVICE_CPU);
    ai_work_node *early = ai_work_add(&graph, "early", AI_OP_NOOP, 50, 1000, AI_DEVICE_CPU);
    ai_work_node *high = ai_work_add(&graph, "high", AI_OP_NOOP, 80, 9000, AI_DEVICE_CPU);

    ai_scheduler scheduler;
    ai_scheduler_init(&scheduler, &graph);
    CHECK(ai_scheduler_pick(&scheduler) == high);
    ai_scheduler_mark_running(&scheduler, high);
    ai_scheduler_mark_done(&scheduler, high);
    CHECK(ai_scheduler_pick(&scheduler) == early);
    ai_scheduler_mark_running(&scheduler, early);
    ai_scheduler_mark_done(&scheduler, early);
    CHECK(ai_scheduler_pick(&scheduler) == late);

    ai_scheduler_mark_running(&scheduler, late);
    ai_scheduler_mark_done(&scheduler, late);
    ai_scheduler_run_report report;
    CHECK(ai_scheduler_run_to_completion(&scheduler, &report));
    CHECK(report.completed_nodes == 3);
    CHECK(scheduler.dispatch_count == 3);
    CHECK(scheduler.candidate_scan_count >= 9);
}

static void test_deadlock_detection(void) {
    reset_runtime();
    ai_work_graph graph;
    ai_work_graph_init(&graph);
    ai_work_node *a = ai_work_add(&graph, "a", AI_OP_NOOP, 1, 1, AI_DEVICE_CPU);
    ai_work_node *b = ai_work_add(&graph, "b", AI_OP_NOOP, 1, 1, AI_DEVICE_CPU);
    ai_work_add_dependency(a, b->id);
    ai_work_add_dependency(b, a->id);

    ai_scheduler scheduler;
    ai_scheduler_run_report report;
    ai_scheduler_init(&scheduler, &graph);
    CHECK(!ai_scheduler_run_to_completion(&scheduler, &report));
    CHECK(report.deadlocked);
    CHECK(scheduler.deadlock_count == 1);
}

static void test_capabilities(void) {
    ai_capability cap = ai_cap_create(
        7, AI_CAP_TENSOR_READ | AI_CAP_MODEL_USE | AI_CAP_GPU_USE
    );
    CHECK(ai_cap_has(&cap, AI_CAP_TENSOR_READ));
    CHECK(ai_cap_has(&cap, AI_CAP_MODEL_USE));
    CHECK(ai_cap_has(&cap, AI_CAP_GPU_USE));
    CHECK(!ai_cap_has(&cap, AI_CAP_TENSOR_WRITE));
    CHECK(!ai_cap_has(&cap, AI_CAP_NETWORK));
    CHECK(!ai_cap_has(&cap, AI_CAP_ADMIN));
}

static void test_memory_metrics(void) {
    reset_runtime();
    CHECK(early_heap_can_alloc(32, 16));
    (void)kalloc(7, 1);
    (void)kalloc(16, 16);

    early_heap_stats stats;
    early_heap_get_stats(&stats);
    CHECK(stats.allocations == 2);
    CHECK(stats.requested_bytes == 23);
    CHECK(stats.padding_bytes == 9);
    CHECK(stats.used == 32);
    CHECK(stats.high_watermark == 32);
    CHECK(!early_heap_can_alloc(8, 3));
}

static void test_benchmark(void) {
    reset_runtime();
    ai_benchmark_report report;
    CHECK(ai_benchmark_run(&report));
    CHECK(report.scenarios_run == 4);
    CHECK(report.scenarios_completed == 4);
    CHECK(report.nodes_total == 60);
    CHECK(report.edges_total == 56);
    CHECK(report.dispatches == 60);
    CHECK(report.candidates_scanned > report.dispatches);
    CHECK(report.selection_efficiency_ppm > 0);
}

static void test_phase14_suite(void) {
    reset_runtime();
    ai_phase14_report report;
    CHECK(ai_phase14_run(&report));
    CHECK(report.passed);
    CHECK(report.assertions_failed == 0);
    CHECK(report.stress_rounds_completed == AI_PHASE14_STRESS_ROUNDS);
    CHECK(report.deadlocks_detected == 1);
    CHECK(report.graphs_validated == AI_PHASE14_STRESS_ROUNDS + 1);
    CHECK(report.graphs_completed == AI_PHASE14_STRESS_ROUNDS + 1);
    CHECK(report.capability_isolation_ok);
    CHECK(report.deadlock_detection_ok);
    CHECK(report.tensor_accounting_ok);
    CHECK(report.memory_accounting_ok);
    CHECK(report.benchmark.passed);
    CHECK(report.heap_growth > 0);
    CHECK(report.heap_after < early_heap_capacity());
}

static void test_malformed_ai_input_fuzz(void) {
    reset_runtime();
    ai_tensor *t = NULL;
    u64 shape[2] = {4, 8};

    CHECK(ai_tensor_create_safe(NULL, AI_DTYPE_F32, 2, shape, AI_LOC_CPU_RAM, 0, &t) != 0);
    CHECK(t == NULL);

    CHECK(ai_tensor_create_safe("t", AI_DTYPE_F32, 2, NULL, AI_LOC_CPU_RAM, 0, &t) != 0);
    CHECK(t == NULL);

    CHECK(ai_tensor_create_safe("t", AI_DTYPE_F32, 0, shape, AI_LOC_CPU_RAM, 0, &t) != 0);
    CHECK(t == NULL);

    CHECK(ai_tensor_create_safe("t", AI_DTYPE_F32, AI_MAX_DIMS + 1, shape, AI_LOC_CPU_RAM, 0, &t) != 0);
    CHECK(t == NULL);

    CHECK(ai_tensor_create_safe("t", (ai_dtype)999, 2, shape, AI_LOC_CPU_RAM, 0, &t) != 0);
    CHECK(t == NULL);

    u64 zero_shape[2] = {4, 0};
    CHECK(ai_tensor_create_safe("t", AI_DTYPE_F32, 2, zero_shape, AI_LOC_CPU_RAM, 0, &t) != 0);
    CHECK(t == NULL);

    u64 huge_shape[2] = {~0ull, ~0ull};
    CHECK(ai_tensor_create_safe("t", AI_DTYPE_F32, 2, huge_shape, AI_LOC_CPU_RAM, 0, &t) != 0);
    CHECK(t == NULL);

    CHECK(ai_tensor_create_safe("t", AI_DTYPE_F32, 2, shape, (ai_tensor_location)999, 0, &t) != 0);
    CHECK(t == NULL);

    ai_work_graph graph;
    ai_work_graph_init(&graph);
    ai_work_node *node = NULL;

    CHECK(ai_work_add_safe(NULL, "node", AI_OP_NOOP, 1, 100, AI_DEVICE_CPU, &node) != 0);
    CHECK(node == NULL);

    CHECK(ai_work_add_safe(&graph, NULL, AI_OP_NOOP, 1, 100, AI_DEVICE_CPU, &node) != 0);
    CHECK(node == NULL);

    CHECK(ai_work_add_safe(&graph, "node", AI_OP_NOOP, 1, 100, 0, &node) != 0);
    CHECK(node == NULL);

    CHECK(ai_work_add_safe(&graph, "valid_node", AI_OP_NOOP, 1, 100, AI_DEVICE_CPU, &node) == 0);
    CHECK(node != NULL);

    CHECK(ai_work_add_dependency_safe(node, 0) != 0);
    CHECK(ai_work_add_input_safe(node, NULL) != 0);
    CHECK(ai_work_add_output_safe(node, NULL) != 0);
}

static u8 g_test_frame_pool[1024 * 4096] __attribute__((aligned(4096)));

static void test_frame_allocator_churn(void) {
    frame_init((uintptr_t)g_test_frame_pool, 1024);
    CHECK(frame_total_count() == 1024);
    CHECK(frame_free_count() == 1024);

    bool churn_ok = true;
    uintptr_t batch[16];
    for (unsigned cycle = 0; cycle < 10000; ++cycle) {
        for (int i = 0; i < 16; ++i) {
            batch[i] = frame_alloc();
            if (batch[i] == 0 || !frame_is_allocated(batch[i])) {
                churn_ok = false;
                break;
            }
        }
        for (int i = 0; i < 16; ++i) {
            frame_free(batch[i]);
            if (frame_is_allocated(batch[i])) {
                churn_ok = false;
                break;
            }
        }
        if (!churn_ok) break;
    }
    CHECK(churn_ok);
    CHECK(frame_free_count() == 1024);
}

static void test_slab_tensor_churn(void) {
    frame_init((uintptr_t)g_test_frame_pool, 1024);
    slab_init();
    ai_tensor_system_init();

    CHECK(ai_tensor_cache != NULL);
    CHECK(ai_work_node_cache != NULL);

    usize initial_frames = kmem_cache_total_frames(ai_tensor_cache);
    CHECK(initial_frames == 0);

    const u64 shape[2] = {16, 16};
    bool churn_ok = true;

    /* Create and destroy 10,000 tensors */
    for (unsigned cycle = 0; cycle < 10000; ++cycle) {
        ai_tensor *t = NULL;
        i32 rc = ai_tensor_create_safe("churn_t", AI_DTYPE_F32, 2, shape, AI_LOC_CPU_RAM, 0, &t);
        if (rc != 0 || t == NULL) {
            churn_ok = false;
            break;
        }
        if (ai_tensor_count() != 1) {
            churn_ok = false;
            break;
        }
        ai_tensor_destroy(t);
        if (ai_tensor_count() != 0) {
            churn_ok = false;
            break;
        }
    }
    CHECK(churn_ok);

    /* High-water mark stays bounded at exactly 1 object */
    usize hw = kmem_cache_high_watermark(ai_tensor_cache);
    CHECK(hw == 1);

    /* Total frames allocated stays bounded (only 1 4KiB page instead of monotonically growing) */
    usize tf = kmem_cache_total_frames(ai_tensor_cache);
    CHECK(tf == 1);
    CHECK(kmem_cache_allocated_objects(ai_tensor_cache) == 0);
    CHECK(ai_tensor_total_bytes() == 0);
}

static void test_slab_work_node_churn(void) {
    ai_work_graph graph;
    ai_work_graph_init(&graph);
    bool work_churn_ok = true;

    /* Create and destroy 1,000 work nodes */
    for (unsigned cycle = 0; cycle < 1000; ++cycle) {
        ai_work_node *node = NULL;
        i32 rc = ai_work_add_safe(&graph, "churn_node", AI_OP_NOOP, 1, 100, AI_DEVICE_CPU, &node);
        if (rc != 0 || node == NULL) {
            work_churn_ok = false;
            break;
        }
        if (graph.node_count != 1) {
            work_churn_ok = false;
            break;
        }
        ai_work_node_destroy(&graph, node);
        if (graph.node_count != 0) {
            work_churn_ok = false;
            break;
        }
    }
    CHECK(work_churn_ok);
    CHECK(kmem_cache_high_watermark(ai_work_node_cache) == 1);
    CHECK(kmem_cache_total_frames(ai_work_node_cache) == 1);
    CHECK(kmem_cache_allocated_objects(ai_work_node_cache) == 0);
}

extern bool nexora_x86_gdt_is_loaded(void);
extern const u64 *nexora_x86_get_gdt(void);

static void test_idt_and_privilege_boundary(void) {
    /* Test GDT initialization */
    nexora_x86_gdt_init(0x7fff0000);
    CHECK(nexora_x86_gdt_is_loaded());
    const u64 *gdt = nexora_x86_get_gdt();
    CHECK(gdt[0] == 0);
    CHECK(gdt[1] == 0x00AF9A000000FFFFull); /* Ring 0 code */
    CHECK(gdt[2] == 0x00CF92000000FFFFull); /* Ring 0 data */
    CHECK(gdt[3] == 0x00CFF2000000FFFFull); /* Ring 3 data */
    CHECK(gdt[4] == 0x00AFFA000000FFFFull); /* Ring 3 code */

    /* Test Syscall MSR / per-CPU init */
    nexora_x86_syscall_init(0x7fff0000);
    CHECK(nexora_bsp_percpu.kernel_rsp == 0x7fff0000);
    CHECK(nexora_bsp_percpu.cpu_id == 0);
    CHECK(nexora_bsp_percpu.user_rsp == 0);

    /* Test IDT initialization and gate descriptors */
    idt_init();
    CHECK(idt_is_loaded());

    const struct idt_entry64 *gate3 = idt_get_entry(3);
    CHECK(gate3->selector == NEXORA_GDT_KERNEL_CODE);
    CHECK(gate3->type_attr == IDT_GATE_USER_TRAP); /* DPL=3, Present, 64-bit Interrupt */

    const struct idt_entry64 *gate14 = idt_get_entry(14);
    CHECK(gate14->selector == NEXORA_GDT_KERNEL_CODE);
    CHECK(gate14->type_attr == IDT_GATE_INTERRUPT); /* DPL=0, Present, 64-bit Interrupt */

    /* Test deliberate int3 execution and counter */
    u64 bp_before = idt_breakpoint_count();
    idt_test_breakpoint();
    CHECK(idt_breakpoint_count() == bp_before + 1);

    /* Test AI backend bridge installation */
    ai_backend_bridge_install();
    CHECK(nexora_backend_get() != NULL);
}

struct test_map_record {
    uintptr_t vaddr;
    size_t memsz;
    size_t filesz;
    uint32_t flags;
};

static struct test_map_record g_test_maps[8];
static u32 g_test_map_count = 0;

static nexora_status_t test_vm_map_segment(
    struct nexora_process *process,
    uintptr_t vaddr,
    size_t memsz,
    const void *src,
    size_t filesz,
    uint32_t flags,
    void *context
) {
    (void)process;
    (void)src;
    (void)context;
    if (g_test_map_count < 8) {
        g_test_maps[g_test_map_count].vaddr = vaddr;
        g_test_maps[g_test_map_count].memsz = memsz;
        g_test_maps[g_test_map_count].filesz = filesz;
        g_test_maps[g_test_map_count].flags = flags;
        g_test_map_count++;
    }
    return NEXORA_OK;
}

static bool test_permissive_validator(const struct nexora_process *process,
                                      uintptr_t address,
                                      size_t length,
                                      bool write) {
    (void)process;
    (void)address;
    (void)length;
    (void)write;
    return true;
}

static void test_ring3_elf_load_and_syscall(void) {
    nexora_process_system_init();
    struct nexora_process user_proc;
    CHECK(nexora_process_init(&user_proc, 100, 1, UINTPTR_MAX) == NEXORA_OK);
    nexora_uaccess_set_validator(test_permissive_validator);

    ai_backend_bridge_install();

    g_test_map_count = 0;
    struct nexora_user_vm_ops vm_ops = {
        .map_segment = test_vm_map_segment,
    };
    uintptr_t entry_rip = 0;
    nexora_status_t status = nexora_elf64_load(
        g_demo_elf,
        g_demo_elf_size,
        &user_proc,
        &vm_ops,
        NULL,
        &entry_rip
    );
    CHECK(status == NEXORA_OK);
    CHECK(entry_rip == 0x40000000);
    CHECK(g_test_map_count > 0);

    nexora_syscall_set_current_process(&user_proc);

    /* Syscall: ABI Query */
    struct nexora_abi_info abi = {0};
    int64_t ret = nexora_syscall_dispatch(NEXORA_SYS_ABI_QUERY, (uintptr_t)&abi, 0, 0, 0, 0, 0);
    CHECK(ret == NEXORA_OK);
    CHECK(abi.abi_version == NEXORA_ABI_VERSION);
    CHECK(abi.syscall_count == NEXORA_SYS_MAX);

    /* Syscall: Tensor Create */
    struct nexora_tensor_desc tensor_req = {
        .struct_size = sizeof(tensor_req),
        .dtype = NEXORA_DTYPE_F16,
        .ndim = 2,
        .location = NEXORA_LOC_CPU_RAM,
        .flags = NEXORA_TENSOR_EPHEMERAL,
        .shape = {32, 32},
    };
    nexora_handle_t tensor_h = 0;
    ret = nexora_syscall_dispatch(NEXORA_SYS_AI_TENSOR_CREATE, (uintptr_t)&tensor_req, (uintptr_t)&tensor_h, 0, 0, 0, 0);
    CHECK(ret == NEXORA_OK);
    CHECK(tensor_h != 0);

    /* Mapping is intentionally unavailable until a real user-VM mapping exists. */
    struct nexora_tensor_map map_req = {
        .struct_size = sizeof(map_req),
        .flags = NEXORA_MAP_READ,
        .offset = 0,
        .length = 64,
    };
    uintptr_t mapped_address = 0;
    ret = nexora_syscall_dispatch(NEXORA_SYS_AI_TENSOR_MAP, tensor_h,
                                  (uintptr_t)&map_req, (uintptr_t)&mapped_address, 0, 0, 0);
    CHECK(ret == NEXORA_ERR(NEXORA_ENOSYS));
    CHECK(mapped_address == 0);

    /* Syscall: Work Submit */
    struct nexora_work_desc work_req = {
        .struct_size = sizeof(work_req),
        .op = NEXORA_OP_NOOP,
        .priority = 10,
        .device_mask = NEXORA_DEVICE_CPU,
        .deadline_ns = 5000,
        .batch_id = 1,
        .input_count = 1,
        .output_count = 0,
        .inputs = { tensor_h },
    };
    nexora_handle_t work_h = 0;
    ret = nexora_syscall_dispatch(NEXORA_SYS_AI_WORK_SUBMIT, (uintptr_t)&work_req, (uintptr_t)&work_h, 0, 0, 0, 0);
    CHECK(ret == NEXORA_OK);
    CHECK(work_h != 0);

    /* Syscall: Work Wait */
    struct nexora_work_result work_res = { .struct_size = sizeof(work_res) };
    ret = nexora_syscall_dispatch(NEXORA_SYS_AI_WORK_WAIT, work_h, 1000000, (uintptr_t)&work_res, 0, 0, 0);
    CHECK(ret == NEXORA_OK);
    CHECK(work_res.state == NEXORA_WORK_DONE);

    /* Delegation retains the object: releasing the parent handle must not free it. */
    struct nexora_process child_proc;
    CHECK(nexora_process_init(&child_proc, 101, 1, UINTPTR_MAX) == NEXORA_OK);
    nexora_process_set_parent(&child_proc, user_proc.pid);

    struct nexora_cap_delegate delegate = {
        .struct_size = sizeof(delegate),
        .target_pid = child_proc.pid,
        .source_handle = tensor_h,
        .rights = NEXORA_RIGHT_READ | NEXORA_RIGHT_RELEASE,
        .delegated_handle = 0,
    };
    ret = nexora_syscall_dispatch(NEXORA_SYS_AI_CAP_DELEGATE, (uintptr_t)&delegate, 0, 0, 0, 0, 0);
    CHECK(ret == NEXORA_OK);
    CHECK(delegate.delegated_handle != 0);

    ret = nexora_syscall_dispatch(NEXORA_SYS_AI_TENSOR_RELEASE, tensor_h, 0, 0, 0, 0, 0);
    CHECK(ret == NEXORA_OK);

    void *delegated_object = NULL;
    CHECK(nexora_handle_resolve(&child_proc.handles, delegate.delegated_handle,
                                NEXORA_HANDLE_TENSOR, NEXORA_RIGHT_READ,
                                &delegated_object, NULL) == NEXORA_OK);
    CHECK(delegated_object != NULL);
    CHECK(ai_tensor_validate((const ai_tensor *)delegated_object));

    nexora_syscall_set_current_process(&child_proc);
    ret = nexora_syscall_dispatch(NEXORA_SYS_AI_TENSOR_RELEASE,
                                  delegate.delegated_handle, 0, 0, 0, 0, 0);
    CHECK(ret == NEXORA_OK);
    nexora_syscall_process_cleanup(&child_proc);
    CHECK(!child_proc.alive);

    nexora_syscall_set_current_process(&user_proc);
    nexora_syscall_process_cleanup(&user_proc);
    CHECK(!user_proc.alive);
}

static void test_exception_handling_fixup_and_recovery(void) {
    /* Test fixup table lookup */
    uintptr_t fault_ip = 0xdeadbeef;
    uintptr_t fixup_ip = 0xfeedface;
    nexora_exception_fixup_register(fault_ip, fixup_ip);
    CHECK(nexora_exception_fixup_lookup(fault_ip) == fixup_ip);
    CHECK(nexora_exception_fixup_lookup(0x12345678) == 0);

    /* Test kernel mode fault with fixup */
    u64 pf_before = idt_page_fault_count();
    struct interrupt_frame kframe = {
        .vector = 14,
        .error_code = 0,
        .rip = fault_ip,
        .cs = NEXORA_GDT_KERNEL_CODE,
        .rflags = 0x202,
        .rsp = 0,
        .ss = NEXORA_GDT_KERNEL_DATA,
    };
    isr_common_handler(&kframe);
    CHECK(idt_page_fault_count() == pf_before + 1);
    CHECK(kframe.rip == fixup_ip);

    /* Test user mode fault recovery */
    struct nexora_process user_p;
    CHECK(nexora_process_init(&user_p, 200, 0x10000, 0x7ffffffff000ull) == NEXORA_OK);
    nexora_syscall_set_current_process(&user_p);
    CHECK(user_p.alive);

    struct interrupt_frame uframe = {
        .vector = 14,
        .error_code = 0x04,
        .rip = 0x40001000,
        .cs = NEXORA_GDT_USER_CODE | 3,
        .rflags = 0x202,
        .rsp = 0x7fffffffe000ull,
        .ss = NEXORA_GDT_USER_DATA | 3,
    };
    isr_common_handler(&uframe);
    CHECK(idt_page_fault_count() == pf_before + 2);
    CHECK(!user_p.alive);
    CHECK(nexora_syscall_current_process() == NULL);
}

static void test_async_queue_scheduler(void) {
    ai_async_queue q;
    ai_async_queue_init(&q);
    CHECK(ai_async_queue_is_empty(&q));
    CHECK(q.count == 0);

    ai_work_node n1 = {.id = 1, .state = AI_WORK_RUNNING};
    ai_work_node n2 = {.id = 2, .state = AI_WORK_RUNNING};
    ai_work_node n3 = {.id = 3, .state = AI_WORK_RUNNING};

    CHECK(ai_async_queue_enqueue(&q, &n1));
    CHECK(ai_async_queue_enqueue(&q, &n2));
    CHECK(ai_async_queue_enqueue(&q, &n3));
    CHECK(!ai_async_queue_is_empty(&q));
    CHECK(q.count == 3);
    CHECK(q.enqueued_count == 3);

    ai_work_node *d = ai_async_queue_dequeue(&q);
    CHECK(d == &n1);
    CHECK(q.count == 2);

    /* Drain remainder */
    u32 drained = ai_async_queue_drain(&q, NULL);
    CHECK(drained == 2);
    CHECK(ai_async_queue_is_empty(&q));
    CHECK(n2.state == AI_WORK_DONE);
    CHECK(n3.state == AI_WORK_DONE);
}

int main(void) {
    printf("TAP version 13\n");
    test_tensor_accounting();
    test_graph_validation();
    test_scheduler();
    test_deadlock_detection();
    test_capabilities();
    test_memory_metrics();
    test_benchmark();
    test_phase14_suite();
    test_malformed_ai_input_fuzz();
    test_frame_allocator_churn();
    test_slab_tensor_churn();
    test_slab_work_node_churn();
    test_idt_and_privilege_boundary();
    test_ring3_elf_load_and_syscall();
    test_exception_handling_fixup_and_recovery();
    test_async_queue_scheduler();
    printf("1..%u\n", tests_run);
    printf("Phase 14 host verification: %s (%u/%u passed)\n",
           tests_failed == 0 ? "PASS" : "FAIL",
           tests_run - tests_failed,
           tests_run);
    return tests_failed == 0 ? 0 : 1;
}
