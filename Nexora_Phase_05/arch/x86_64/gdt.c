#include <nexora/x86_64.h>
#include <stdint.h>

struct __attribute__((packed)) gdtr64 {
    uint16_t limit;
    uint64_t base;
};

struct __attribute__((packed)) tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
};

static uint64_t gdt[7] __attribute__((aligned(16)));
static struct tss64 tss __attribute__((aligned(16)));

static void set_tss_descriptor(uint64_t base, uint32_t limit) {
    uint64_t low = 0;
    low |= (uint64_t)(limit & 0xFFFFu);
    low |= (base & 0xFFFFFFull) << 16u;
    low |= (uint64_t)0x89u << 40u;
    low |= (uint64_t)((limit >> 16u) & 0xFu) << 48u;
    low |= ((base >> 24u) & 0xFFull) << 56u;
    gdt[5] = low;
    gdt[6] = base >> 32u;
}

void nexora_x86_set_kernel_stack(uintptr_t rsp0) {
    tss.rsp0 = (uint64_t)rsp0;
}

void nexora_x86_gdt_init(uintptr_t rsp0) {
    for (uint32_t i = 0; i < 7u; ++i) gdt[i] = 0;
    unsigned char *tss_bytes = (unsigned char *)&tss;
    for (size_t i = 0; i < sizeof(tss); ++i) tss_bytes[i] = 0;

    gdt[0] = 0x0000000000000000ull;
    gdt[1] = 0x00AF9A000000FFFFull; /* ring-0 64-bit code */
    gdt[2] = 0x00CF92000000FFFFull; /* ring-0 data */
    gdt[3] = 0x00CFF2000000FFFFull; /* ring-3 data */
    gdt[4] = 0x00AFFA000000FFFFull; /* ring-3 64-bit code */

    tss.rsp0 = (uint64_t)rsp0;
    tss.iomap_base = sizeof(tss);
    set_tss_descriptor((uint64_t)(uintptr_t)&tss, (uint32_t)(sizeof(tss) - 1u));

    struct gdtr64 gdtr = {
        .limit = (uint16_t)(sizeof(gdt) - 1u),
        .base = (uint64_t)(uintptr_t)gdt,
    };

    __asm__ volatile("lgdt %0" : : "m"(gdtr) : "memory");
    __asm__ volatile(
        "mov %0, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        : : "i"(NEXORA_GDT_KERNEL_DATA) : "rax", "memory");
    uint16_t tss_selector = NEXORA_GDT_TSS;
    __asm__ volatile("ltr %0" : : "r"(tss_selector) : "memory");
}
