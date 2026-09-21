/*
 * Multiboot2 entry + transition to x86-64 long mode.
 *
 * GRUB enters a Multiboot2 kernel in 32-bit protected mode.
 * We build minimal identity-mapped page tables, enable PAE and long mode,
 * then jump into 64-bit code and call kmain().
 */

.set MB2_MAGIC,       0xE85250D6
.set MB2_ARCH,        0
.set MB2_HEADER_LEN,  multiboot_header_end - multiboot_header
.set MB2_CHECKSUM,    -(MB2_MAGIC + MB2_ARCH + MB2_HEADER_LEN)

.section .multiboot_header
.align 8
multiboot_header:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long MB2_HEADER_LEN
    .long MB2_CHECKSUM

    /* End tag */
    .short 0
    .short 0
    .long 8
multiboot_header_end:

.section .text
.global _start
.extern kmain

.code32
_start:
    cli

    /* Preserve Multiboot2 information pointer from EBX. */
    mov %ebx, %edi

    /* Clear page tables. */
    lea pml4_table, %eax
    mov %eax, %edi
    xor %eax, %eax
    mov $((4096 * 3) / 4), %ecx
    rep stosl

    /*
     * PML4[0] -> PDPT
     * PDPT[0] -> PD
     */
    lea pdpt_table, %eax
    or $0x3, %eax
    mov %eax, pml4_table

    lea pd_table, %eax
    or $0x3, %eax
    mov %eax, pdpt_table

    /*
     * Identity map first 1 GiB with 2 MiB pages.
     * PD entry = physical_address | present | writable | huge_page
     */
    xor %ecx, %ecx
1:
    mov %ecx, %eax
    shl $21, %eax
    or $0x83, %eax
    mov %eax, pd_table(,%ecx,8)
    movl $0, pd_table+4(,%ecx,8)
    inc %ecx
    cmp $512, %ecx
    jne 1b

    /* Enable PAE. */
    mov %cr4, %eax
    or $(1 << 5), %eax
    mov %eax, %cr4

    /* Load PML4. */
    lea pml4_table, %eax
    mov %eax, %cr3

    /* Enable long mode in EFER.LME. */
    mov $0xC0000080, %ecx
    rdmsr
    or $(1 << 8), %eax
    wrmsr

    /* Enable paging. */
    mov %cr0, %eax
    or $(1 << 31), %eax
    mov %eax, %cr0

    lgdt gdt64_ptr
    ljmp $0x08, $long_mode_start

.code64
long_mode_start:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs

    lea stack_top(%rip), %rsp
    xor %rbp, %rbp

    /*
     * Multiboot2 pointer is not consumed yet in v0.1.
     * kmain takes no arguments for now.
     */
    call kmain

2:
    cli
    hlt
    jmp 2b

.align 8
gdt64:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt64_end:

gdt64_ptr:
    .word gdt64_end - gdt64 - 1
    .quad gdt64

.section .bss
.align 4096
pml4_table:
    .skip 4096
pdpt_table:
    .skip 4096
pd_table:
    .skip 4096

.align 16
stack_bottom:
    .skip 65536
stack_top:
