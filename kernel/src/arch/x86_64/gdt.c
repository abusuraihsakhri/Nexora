#include <kernel/x86_64.h>

struct __attribute__((packed)) gdtr64 {
    u16 limit;
    u64 base;
};

struct __attribute__((packed)) tss64 {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist1;
    u64 ist2;
    u64 ist3;
    u64 ist4;
    u64 ist5;
    u64 ist6;
    u64 ist7;
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
};

static u64 gdt[7] __attribute__((aligned(16)));
static struct tss64 tss __attribute__((aligned(16)));
static bool gdt_loaded = false;

static void set_tss_descriptor(u64 base, u32 limit) {
    u64 low = 0;
    low |= (u64)(limit & 0xFFFFu);
    low |= (base & 0xFFFFFFull) << 16u;
    low |= (u64)0x89u << 40u;
    low |= (u64)((limit >> 16u) & 0xFu) << 48u;
    low |= ((base >> 24u) & 0xFFull) << 56u;
    gdt[5] = low;
    gdt[6] = base >> 32u;
}

void nexora_x86_set_kernel_stack(uintptr_t rsp0) {
    tss.rsp0 = (u64)rsp0;
}

void nexora_x86_gdt_init(uintptr_t rsp0) {
    for (u32 i = 0; i < 7u; ++i) gdt[i] = 0;
    u8 *tss_bytes = (u8 *)&tss;
    for (usize i = 0; i < sizeof(tss); ++i) tss_bytes[i] = 0;

    gdt[0] = 0x0000000000000000ull;
    gdt[1] = 0x00AF9A000000FFFFull; /* ring-0 64-bit code */
    gdt[2] = 0x00CF92000000FFFFull; /* ring-0 data */
    gdt[3] = 0x00CFF2000000FFFFull; /* ring-3 data */
    gdt[4] = 0x00AFFA000000FFFFull; /* ring-3 64-bit code */

    tss.rsp0 = (u64)rsp0;
    tss.iomap_base = sizeof(tss);
    set_tss_descriptor((u64)(uintptr_t)&tss, (u32)(sizeof(tss) - 1u));

    struct gdtr64 gdtr = {
        .limit = (u16)(sizeof(gdt) - 1u),
        .base = (u64)(uintptr_t)gdt,
    };

#if !defined(HOST_TEST)
    __asm__ volatile("lgdt %0" : : "m"(gdtr) : "memory");
    __asm__ volatile(
        "mov %0, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        : : "i"(NEXORA_GDT_KERNEL_DATA) : "rax", "memory");
    u16 tss_selector = NEXORA_GDT_TSS;
    __asm__ volatile("ltr %0" : : "r"(tss_selector) : "memory");
#else
    (void)gdtr;
#endif
    gdt_loaded = true;
}

bool nexora_x86_gdt_is_loaded(void) {
    return gdt_loaded;
}

const u64 *nexora_x86_get_gdt(void) {
    return gdt;
}
