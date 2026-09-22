#ifndef NEXORA_AI_FAULT_INJECTION_H
#define NEXORA_AI_FAULT_INJECTION_H

#include <kernel/types.h>

/* Mutable injector state is single-writer; use per-CPU/domain instances or a lock. */
#define AI_FAULT_MAX_RULES 32U

typedef enum {
    AI_FAULT_ALLOC = 1,
    AI_FAULT_MAP,
    AI_FAULT_SCHED_DISPATCH,
    AI_FAULT_DEVICE_SUBMIT,
    AI_FAULT_DMA,
    AI_FAULT_REMOTE_SEND,
    AI_FAULT_REMOTE_RECV,
    AI_FAULT_CAPABILITY,
    AI_FAULT_TIMEOUT,
    AI_FAULT_CUSTOM = 255
} ai_fault_point;

typedef enum {
    AI_FAULT_DISABLED = 0,
    AI_FAULT_ONCE,
    AI_FAULT_EVERY_N,
    AI_FAULT_ALWAYS
} ai_fault_mode;

typedef struct {
    u32 point;
    u32 mode;
    u32 every_n;
    u32 seen;
    u32 fired;
    bool enabled;
} ai_fault_rule;

typedef struct {
    ai_fault_rule rules[AI_FAULT_MAX_RULES];
    u32 rule_count;
} ai_fault_injector;

void ai_fault_init(ai_fault_injector *injector);
bool ai_fault_configure(
    ai_fault_injector *injector,
    ai_fault_point point,
    ai_fault_mode mode,
    u32 every_n
);
void ai_fault_disable(ai_fault_injector *injector, ai_fault_point point);
bool ai_fault_should_fail(ai_fault_injector *injector, ai_fault_point point);
u32 ai_fault_fired_count(const ai_fault_injector *injector, ai_fault_point point);

#endif
