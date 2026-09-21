#ifndef AIKERNEL_ATOMIC_H
#define AIKERNEL_ATOMIC_H

#include <kernel/types.h>

static inline u64 ai_atomic_load_u64(const volatile u64 *value) {
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static inline void ai_atomic_store_u64(volatile u64 *value, u64 desired) {
    __atomic_store_n(value, desired, __ATOMIC_RELEASE);
}

static inline u32 ai_atomic_load_u32(const volatile u32 *value) {
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static inline void ai_atomic_store_u32(volatile u32 *value, u32 desired) {
    __atomic_store_n(value, desired, __ATOMIC_RELEASE);
}

static inline bool ai_atomic_compare_exchange_u64(
    volatile u64 *value,
    u64 *expected,
    u64 desired
) {
    return __atomic_compare_exchange_n(
        value,
        expected,
        desired,
        false,
        __ATOMIC_ACQ_REL,
        __ATOMIC_ACQUIRE
    );
}

static inline bool ai_atomic_compare_exchange_u32(
    volatile u32 *value,
    u32 *expected,
    u32 desired
) {
    return __atomic_compare_exchange_n(
        value,
        expected,
        desired,
        false,
        __ATOMIC_ACQ_REL,
        __ATOMIC_ACQUIRE
    );
}

static inline u64 ai_atomic_fetch_add_u64(volatile u64 *value, u64 add) {
    return __atomic_fetch_add(value, add, __ATOMIC_ACQ_REL);
}

static inline u64 ai_atomic_fetch_sub_u64(volatile u64 *value, u64 sub) {
    return __atomic_fetch_sub(value, sub, __ATOMIC_ACQ_REL);
}

static inline u32 ai_atomic_fetch_add_u32(volatile u32 *value, u32 add) {
    return __atomic_fetch_add(value, add, __ATOMIC_ACQ_REL);
}

static inline u32 ai_atomic_fetch_sub_u32(volatile u32 *value, u32 sub) {
    return __atomic_fetch_sub(value, sub, __ATOMIC_ACQ_REL);
}

#endif
