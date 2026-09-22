#ifndef AIKERNEL_TYPES_H
#define AIKERNEL_TYPES_H

/*
 * Use the compiler's standard integer/pointer-width definitions even for the
 * freestanding kernel. These headers define types only; they do not require a
 * hosted C runtime and keep host tests ABI-compatible with kernel code.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;

typedef size_t    usize;
typedef ptrdiff_t isize;

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif
