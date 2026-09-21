OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_user_start)
SECTIONS
{
    . = 0x0000000040000000;
    .text : ALIGN(4096) { *(.text*) }
    .rodata : ALIGN(4096) { *(.rodata*) }
    .data : ALIGN(4096) { *(.data*) }
    .bss : ALIGN(4096) { *(COMMON) *(.bss*) }
}
