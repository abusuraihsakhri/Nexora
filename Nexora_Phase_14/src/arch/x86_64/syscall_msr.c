#include <kernel/x86_64.h>

#define IA32_EFER   0xC0000080u
#define IA32_STAR   0xC0000081u
#define IA32_LSTAR  0xC0000082u
#define IA32_FMASK  0xC0000084u
#define IA32_KERNEL_GS_BASE 0xC0000102u
#define EFER_SCE    (1ull << 0)

#if defined(HOST_TEST)
void nexora_syscall_entry(void) {}
#else
extern void nexora_syscall_entry(void);
#endif

struct nexora_percpu nexora_bsp_percpu;
static bool s_syscall_initialized = false;

#if !defined(HOST_TEST)
static u64 rdmsr(u32 msr) {
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32u) | lo;
}

static void wrmsr(u32 msr, u64 value) {
    u32 lo = (u32)value;
    u32 hi = (u32)(value >> 32u);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}
#endif

void nexora_x86_syscall_init(uintptr_t kernel_stack_top) {
    uintptr_t aligned_stack_top = kernel_stack_top & ~(uintptr_t)0xFull;
    nexora_percpu_init(&nexora_bsp_percpu, 0, aligned_stack_top);
    nexora_x86_set_kernel_stack(aligned_stack_top);

#if !defined(HOST_TEST)
    u64 efer = rdmsr(IA32_EFER);
    wrmsr(IA32_EFER, efer | EFER_SCE);

    /*
     * SYSCALL loads CS=0x08, SS=0x10.
     * SYSRET loads SS=(0x13+8)|3=0x1b and CS=(0x13+16)|3=0x23.
     */
    u64 star = ((u64)0x13u << 48u) |
               ((u64)NEXORA_GDT_KERNEL_CODE << 32u);
    wrmsr(IA32_STAR, star);
    wrmsr(IA32_LSTAR, (u64)(uintptr_t)&nexora_syscall_entry);

    /* swapgs selects the single BSP per-CPU record until SMP bring-up exists. */
    wrmsr(IA32_KERNEL_GS_BASE, (u64)(uintptr_t)&nexora_bsp_percpu);

    /* Clear IF, TF, DF and AC while executing in the kernel. */
    wrmsr(IA32_FMASK, (1ull << 9) | (1ull << 8) | (1ull << 10) | (1ull << 18));
#endif
    s_syscall_initialized = true;
}

bool nexora_x86_syscall_is_initialized(void) {
    return s_syscall_initialized;
}
