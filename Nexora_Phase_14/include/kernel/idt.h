#ifndef AIKERNEL_IDT_H
#define AIKERNEL_IDT_H

#include <kernel/types.h>

#define IDT_ENTRIES 256

#define IDT_GATE_INTERRUPT 0x8Eu /* 64-bit Interrupt Gate, DPL=0, Present */
#define IDT_GATE_USER_TRAP 0xEEu /* 64-bit Interrupt Gate, DPL=3, Present (int3 accessible) */

struct __attribute__((packed)) idt_entry64 {
    u16 offset_low;   /* bits 0..15 */
    u16 selector;     /* code segment selector (0x08 for kernel code) */
    u8  ist;          /* bits 0..2 = IST, rest 0 */
    u8  type_attr;    /* type & attributes */
    u16 offset_mid;   /* bits 16..31 */
    u32 offset_high;  /* bits 32..63 */
    u32 zero;         /* reserved = 0 */
};

struct __attribute__((packed)) idtr64 {
    u16 limit;
    u64 base;
};

struct interrupt_frame {
    u64 r11;
    u64 r10;
    u64 r9;
    u64 r8;
    u64 rdi;
    u64 rsi;
    u64 rdx;
    u64 rcx;
    u64 rax;
    u64 vector;
    u64 error_code;
    u64 rip;
    u64 cs;
    u64 rflags;
    u64 rsp;
    u64 ss;
};

void idt_init(void);
void idt_set_gate(u8 vector, void *handler, u16 selector, u8 type_attr, u8 ist);
void idt_test_breakpoint(void);
void idt_test_page_fault(uintptr_t fault_rip, bool user_mode);
u64  idt_breakpoint_count(void);
u64  idt_page_fault_count(void);
u64  idt_gp_count(void);
const struct idt_entry64 *idt_get_entry(u8 vector);
bool idt_is_loaded(void);

void isr_common_handler(struct interrupt_frame *frame);

#endif
