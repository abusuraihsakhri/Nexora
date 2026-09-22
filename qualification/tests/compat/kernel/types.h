#ifndef AIKERNEL_TYPES_H
#define AIKERNEL_TYPES_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed char        i8;
typedef signed short       i16;
typedef signed int         i32;
typedef signed long long   i64;
typedef u64 usize;
typedef i64 isize;
typedef enum { false = 0, true = 1 } bool;
#ifndef NULL
#define NULL ((void*)0)
#endif
#endif
