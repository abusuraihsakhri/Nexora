#include <nexora/x86_64.h>
#include <stdint.h>

#define IA32_EFER   0xC0000080u
#define IA32_STAR   0xC0000081u
#define IA32_LSTAR  0xC0000082u
#define IA32_FMASK  0xC0000084u
#define EFER_SCE    (1ull << 0)

extern void nexora_syscall_entry(void);

static uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32u) | lo;
}

static void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)value;
    uint32_t hi = (uint32_t)(value >> 32u);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}

void nexora_x86_syscall_init(uintptr_t kernel_stack_top) {
    uintptr_t aligned_stack_top = kernel_stack_top & ~(uintptr_t)0xFull;
    nexora_syscall_kernel_rsp = aligned_stack_top;
    nexora_x86_set_kernel_stack(aligned_stack_top);

    uint64_t efer = rdmsr(IA32_EFER);
    wrmsr(IA32_EFER, efer | EFER_SCE);

    /*
     * SYSCALL loads CS=0x08, SS=0x10.
     * SYSRET loads SS=(0x13+8)|3=0x1b and CS=(0x13+16)|3=0x23.
     */
    uint64_t star = ((uint64_t)0x13u << 48u) |
                    ((uint64_t)NEXORA_GDT_KERNEL_CODE << 32u);
    wrmsr(IA32_STAR, star);
    wrmsr(IA32_LSTAR, (uint64_t)(uintptr_t)&nexora_syscall_entry);

    /* Clear IF, TF, DF and AC while in the kernel. */
    wrmsr(IA32_FMASK, (1ull << 9) | (1ull << 8) | (1ull << 10) | (1ull << 18));
}
