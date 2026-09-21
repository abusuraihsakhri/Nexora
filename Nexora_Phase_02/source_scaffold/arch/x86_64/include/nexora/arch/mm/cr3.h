#pragma once
#include <stdint.h>
uint64_t x86_read_cr3(void);
void x86_write_cr3(uint64_t value);
