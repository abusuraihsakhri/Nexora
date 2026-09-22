#ifndef AIKERNEL_TYPES_H
#define AIKERNEL_TYPES_H

/*
 * C's fixed-width and pointer-width integer types are part of the freestanding
 * implementation. Using the standard definitions keeps kernel headers ABI-
 * compatible with Nexora's public headers and prevents duplicate uintptr_t
 * typedefs during host validation.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef size_t    usize;
typedef ptrdiff_t isize;

#ifndef NULL
#define NULL ((void *)0)
#endif

#endif
