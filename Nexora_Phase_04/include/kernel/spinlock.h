#ifndef AIKERNEL_SPINLOCK_H
#define AIKERNEL_SPINLOCK_H

#include <kernel/types.h>

typedef struct {
    volatile u8 value;
} ai_spinlock;

#define AI_SPINLOCK_INITIALIZER { 0 }

static inline void ai_spinlock_init(ai_spinlock *lock) {
    if (lock) {
        __atomic_store_n(&lock->value, 0u, __ATOMIC_RELAXED);
    }
}

static inline void ai_cpu_relax(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile ("pause" ::: "memory");
#else
    __asm__ volatile ("" ::: "memory");
#endif
}

static inline void ai_spin_lock(ai_spinlock *lock) {
    if (!lock) {
        return;
    }

    while (__atomic_test_and_set(&lock->value, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&lock->value, __ATOMIC_RELAXED) != 0u) {
            ai_cpu_relax();
        }
    }
}

static inline bool ai_spin_try_lock(ai_spinlock *lock) {
    if (!lock) {
        return false;
    }
    return !__atomic_test_and_set(&lock->value, __ATOMIC_ACQUIRE);
}

static inline void ai_spin_unlock(ai_spinlock *lock) {
    if (!lock) {
        return;
    }
    __atomic_clear(&lock->value, __ATOMIC_RELEASE);
}

#endif
