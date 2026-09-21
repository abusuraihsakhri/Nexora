#include <ai/release_health.h>

static bool valid_status(ai_health_status status) {
    return status == AI_HEALTH_UNKNOWN || status == AI_HEALTH_PASS ||
           status == AI_HEALTH_WARN || status == AI_HEALTH_FAIL;
}

static ai_health_check *find_check(ai_health_registry *registry, u32 id) {
    if (!registry) return NULL;
    for (u32 i = 0; i < registry->count; ++i) {
        if (registry->checks[i].id == id) return &registry->checks[i];
    }
    return NULL;
}

void ai_health_init(ai_health_registry *registry) {
    if (!registry) return;
    registry->count = 0;
}

bool ai_health_register(
    ai_health_registry *registry,
    u32 id,
    const char *name,
    bool critical
) {
    if (!registry || !name || registry->count >= AI_HEALTH_MAX_CHECKS) return false;
    if (find_check(registry, id)) return false;

    ai_health_check *check = &registry->checks[registry->count++];
    check->id = id;
    check->name = name;
    check->status = (u32)AI_HEALTH_UNKNOWN;
    check->critical = critical;
    check->detail = 0;
    return true;
}

bool ai_health_set(
    ai_health_registry *registry,
    u32 id,
    ai_health_status status,
    u64 detail
) {
    if (!valid_status(status)) return false;
    ai_health_check *check = find_check(registry, id);
    if (!check) return false;
    check->status = (u32)status;
    check->detail = detail;
    return true;
}

ai_health_summary ai_health_summarize(const ai_health_registry *registry) {
    ai_health_summary summary = {0, 0, 0, 0, 0};
    if (!registry) {
        summary.critical_fail = 1;
        return summary;
    }

    for (u32 i = 0; i < registry->count; ++i) {
        const ai_health_check *check = &registry->checks[i];
        switch ((ai_health_status)check->status) {
            case AI_HEALTH_PASS:
                summary.pass++;
                break;
            case AI_HEALTH_WARN:
                summary.warn++;
                break;
            case AI_HEALTH_FAIL:
                summary.fail++;
                if (check->critical) summary.critical_fail++;
                break;
            case AI_HEALTH_UNKNOWN:
            default:
                summary.unknown++;
                if (check->critical) summary.critical_fail++;
                break;
        }
    }
    return summary;
}

bool ai_health_release_ready(const ai_health_registry *registry) {
    ai_health_summary summary = ai_health_summarize(registry);
    return summary.critical_fail == 0 && summary.unknown == 0;
}
