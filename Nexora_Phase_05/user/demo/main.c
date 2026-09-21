#include "../libnexora/nexora.h"

volatile uint64_t nexora_demo_status;
volatile nexora_handle_t nexora_demo_tensor;
volatile nexora_handle_t nexora_demo_work;
volatile uintptr_t nexora_demo_mapping;

void user_main(void) {
    struct nexora_abi_info abi = {0};
    if (nexora_abi_query(&abi) != NEXORA_OK || abi.abi_version != NEXORA_ABI_VERSION) {
        nexora_demo_status = 1;
        for (;;) __asm__ volatile("pause");
    }

    struct nexora_tensor_desc tensor = {
        .struct_size = sizeof(tensor),
        .dtype = NEXORA_DTYPE_F16,
        .ndim = 2,
        .location = NEXORA_LOC_CPU_RAM,
        .flags = NEXORA_TENSOR_EPHEMERAL | NEXORA_TENSOR_ZEROED,
        .shape = {64, 64},
    };
    if (ai_tensor_create(&tensor, (nexora_handle_t *)&nexora_demo_tensor) != NEXORA_OK) {
        nexora_demo_status = 2;
        for (;;) __asm__ volatile("pause");
    }

    struct nexora_tensor_map map = {
        .struct_size = sizeof(map),
        .flags = NEXORA_MAP_READ | NEXORA_MAP_WRITE,
        .offset = 0,
        .length = 0,
        .address_hint = 0,
    };
    uintptr_t address = 0;
    if (ai_tensor_map(nexora_demo_tensor, &map, &address) != NEXORA_OK) {
        nexora_demo_status = 3;
        for (;;) __asm__ volatile("pause");
    }
    nexora_demo_mapping = address;

    struct nexora_work_desc work = {
        .struct_size = sizeof(work),
        .op = NEXORA_OP_NOOP,
        .priority = 10,
        .device_mask = NEXORA_DEVICE_CPU,
        .deadline_ns = 0,
        .batch_id = 1,
        .input_count = 1,
        .output_count = 0,
        .inputs = { nexora_demo_tensor },
    };
    if (ai_work_submit(&work, (nexora_handle_t *)&nexora_demo_work) != NEXORA_OK) {
        nexora_demo_status = 4;
        for (;;) __asm__ volatile("pause");
    }

    struct nexora_work_result result = { .struct_size = sizeof(result) };
    if (ai_work_wait(nexora_demo_work, 1000000000ull, &result) != NEXORA_OK ||
        result.state != NEXORA_WORK_DONE) {
        nexora_demo_status = 5;
        for (;;) __asm__ volatile("pause");
    }

    if (ai_tensor_release(nexora_demo_tensor) != NEXORA_OK) {
        nexora_demo_status = 6;
        for (;;) __asm__ volatile("pause");
    }

    nexora_demo_status = 0x505353ull; /* PSS = Phase-5 success marker */
    for (;;) __asm__ volatile("pause");
}
