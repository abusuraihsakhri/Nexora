#ifndef AIKERNEL_PRINTK_H
#define AIKERNEL_PRINTK_H

#include <kernel/types.h>

void console_init(void);
void kputc(char c);
void kputs(const char *s);
void kprint_u64(u64 value);
void kprint_hex(u64 value);

#endif
