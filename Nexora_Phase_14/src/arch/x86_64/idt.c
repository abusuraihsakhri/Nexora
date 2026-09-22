#include <kernel/idt.h>
#include <kernel/x86_64.h>
#include <kernel/printk.h>
#include <kernel/panic.h>
#include <nexora/syscall.h>

static struct idt_entry64 s_idt[IDT_ENTRIES] __attribute__((aligned(16)));
static struct idtr64 s_idtr;
static bool s_idt_loaded = false;

volatile u64 g_breakpoint_count = 0;
volatile u64 g_page_fault_count = 0;
volatile u64 g_gp_count = 0;

#if defined(HOST_TEST)
void isr3_entry(void) {}
void isr8_entry(void) {}
void isr13_entry(void) {}
void isr14_entry(void) {}
void isr_default_entry(void) {}
#else
extern void isr3_entry(void);
extern void isr8_entry(void);
extern void isr13_entry(void);
extern void isr14_entry(void);
extern void isr_default_entry(void);
#endif

void idt_set_gate(u8 vector, void *handler, u16 selector, u8 type_attr, u8 ist) {
    uintptr_t addr = (uintptr_t)handler;
    s_idt[vector].offset_low = (u16)(addr & 0xFFFFu);
    s_idt[vector].selector = selector;
    s_idt[vector].ist = (u8)(ist & 0x7u);
    s_idt[vector].type_attr = type_attr;
    s_idt[vector].offset_mid = (u16)((addr >> 16u) & 0xFFFFu);
    s_idt[vector].offset_high = (u32)((addr >> 32u) & 0xFFFFFFFFu);
    s_idt[vector].zero = 0;
}

const struct idt_entry64 *idt_get_entry(u8 vector) {
    return &s_idt[vector];
}

bool idt_is_loaded(void) {
    return s_idt_loaded;
}

u64 idt_breakpoint_count(void) {
    return g_breakpoint_count;
}

u64 idt_page_fault_count(void) {
    return g_page_fault_count;
}

u64 idt_gp_count(void) {
    return g_gp_count;
}

extern uintptr_t nexora_exception_fixup_lookup(uintptr_t fault_ip);

static void terminate_current_user_process(const char *reason) {
    struct nexora_process *process = nexora_syscall_current_process();
    if (process) nexora_syscall_process_cleanup(process);
#if defined(HOST_TEST)
    (void)reason;
#else
    /*
     * Phase 14 has no runnable-process scheduler to switch to after a fatal
     * user exception. Fail closed rather than IRET to the same faulting RIP.
     */
    panic(reason);
#endif
}

void isr_common_handler(struct interrupt_frame *frame) {
    if (!frame) {
#if !defined(HOST_TEST)
        panic("null interrupt frame");
#endif
        return;
    }

    if (frame->vector == 3) {
        g_breakpoint_count++;
        return;
    }

    if (frame->vector == 8) {
#if !defined(HOST_TEST)
        panic("double fault");
#else
        return;
#endif
    }

    if (frame->vector == 14) {
        g_page_fault_count++;

        uintptr_t fixup = nexora_exception_fixup_lookup((uintptr_t)frame->rip);
        if (fixup != 0) {
            frame->rip = (u64)fixup;
            return;
        }

        if ((frame->cs & 3u) == 3u) {
            terminate_current_user_process("fatal userspace page fault");
            return;
        }

#if !defined(HOST_TEST)
        panic("unhandled kernel page fault");
#else
        return;
#endif
    }

    if (frame->vector == 13) {
        g_gp_count++;
        if ((frame->cs & 3u) == 3u) {
            terminate_current_user_process("fatal userspace general-protection fault");
            return;
        }
#if !defined(HOST_TEST)
        panic("unhandled kernel general-protection fault");
#else
        return;
#endif
    }

#if !defined(HOST_TEST)
    panic("unhandled CPU exception");
#endif
}

void idt_init(void) {
    for (usize i = 0; i < IDT_ENTRIES; ++i) {
        idt_set_gate((u8)i, (void *)&isr_default_entry, NEXORA_GDT_KERNEL_CODE, IDT_GATE_INTERRUPT, 0);
    }

    idt_set_gate(3, (void *)&isr3_entry, NEXORA_GDT_KERNEL_CODE, IDT_GATE_USER_TRAP, 0);
    idt_set_gate(8, (void *)&isr8_entry, NEXORA_GDT_KERNEL_CODE, IDT_GATE_INTERRUPT, 0);
    idt_set_gate(13, (void *)&isr13_entry, NEXORA_GDT_KERNEL_CODE, IDT_GATE_INTERRUPT, 0);
    idt_set_gate(14, (void *)&isr14_entry, NEXORA_GDT_KERNEL_CODE, IDT_GATE_INTERRUPT, 0);

    s_idtr.limit = (u16)(sizeof(s_idt) - 1u);
    s_idtr.base = (u64)(uintptr_t)s_idt;

#if !defined(HOST_TEST)
    __asm__ volatile("lidt %0" : : "m"(s_idtr) : "memory");
#endif
    s_idt_loaded = true;
}

void idt_test_breakpoint(void) {
#if !defined(HOST_TEST)
    __asm__ volatile("int $3");
#else
    struct interrupt_frame frame = {
        .vector = 3,
        .error_code = 0,
        .rip = (u64)(uintptr_t)&idt_test_breakpoint,
        .cs = NEXORA_GDT_KERNEL_CODE,
        .rflags = 0x202,
        .rsp = 0,
        .ss = NEXORA_GDT_KERNEL_DATA,
    };
    isr_common_handler(&frame);
#endif
}

void idt_test_page_fault(uintptr_t fault_rip, bool user_mode) {
    struct interrupt_frame frame = {
        .vector = 14,
        .error_code = user_mode ? 0x04 : 0x00,
        .rip = (u64)fault_rip,
        .cs = user_mode ? NEXORA_GDT_USER_CODE | 3 : NEXORA_GDT_KERNEL_CODE,
        .rflags = 0x202,
        .rsp = 0,
        .ss = user_mode ? NEXORA_GDT_USER_DATA | 3 : NEXORA_GDT_KERNEL_DATA,
    };
    isr_common_handler(&frame);
}
