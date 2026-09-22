#ifndef NEXORA_AI_RELEASE_HEALTH_H
#define NEXORA_AI_RELEASE_HEALTH_H

#include <kernel/types.h>

/* Registry updates require single-writer ownership or external synchronization.
 * Registered name pointers are borrowed and must remain valid for the registry lifetime. */
#define AI_HEALTH_MAX_CHECKS 32U

typedef enum {
    AI_HEALTH_UNKNOWN = 0,
    AI_HEALTH_PASS,
    AI_HEALTH_WARN,
    AI_HEALTH_FAIL
} ai_health_status;

typedef struct {
    u32 id;
    const char *name;
    u32 status;
    bool critical;
    u64 detail;
} ai_health_check;

typedef struct {
    ai_health_check checks[AI_HEALTH_MAX_CHECKS];
    u32 count;
} ai_health_registry;

typedef struct {
    u32 pass;
    u32 warn;
    u32 fail;
    u32 unknown;
    u32 critical_fail;
} ai_health_summary;

void ai_health_init(ai_health_registry *registry);
bool ai_health_register(
    ai_health_registry *registry,
    u32 id,
    const char *name,
    bool critical
);
bool ai_health_set(
    ai_health_registry *registry,
    u32 id,
    ai_health_status status,
    u64 detail
);
ai_health_summary ai_health_summarize(const ai_health_registry *registry);
bool ai_health_release_ready(const ai_health_registry *registry);

#endif
