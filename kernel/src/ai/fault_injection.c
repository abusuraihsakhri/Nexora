#include <ai/fault_injection.h>

static bool valid_point(ai_fault_point point) {
    switch (point) {
        case AI_FAULT_ALLOC:
        case AI_FAULT_MAP:
        case AI_FAULT_SCHED_DISPATCH:
        case AI_FAULT_DEVICE_SUBMIT:
        case AI_FAULT_DMA:
        case AI_FAULT_REMOTE_SEND:
        case AI_FAULT_REMOTE_RECV:
        case AI_FAULT_CAPABILITY:
        case AI_FAULT_TIMEOUT:
        case AI_FAULT_CUSTOM:
            return true;
        default:
            return false;
    }
}

static bool valid_mode(ai_fault_mode mode) {
    return mode == AI_FAULT_ONCE || mode == AI_FAULT_EVERY_N || mode == AI_FAULT_ALWAYS;
}

static ai_fault_rule *find_rule(ai_fault_injector *injector, ai_fault_point point) {
    if (!injector) return NULL;
    for (u32 i = 0; i < injector->rule_count; ++i) {
        if (injector->rules[i].point == (u32)point) return &injector->rules[i];
    }
    return NULL;
}

static const ai_fault_rule *find_rule_const(const ai_fault_injector *injector, ai_fault_point point) {
    if (!injector) return NULL;
    for (u32 i = 0; i < injector->rule_count; ++i) {
        if (injector->rules[i].point == (u32)point) return &injector->rules[i];
    }
    return NULL;
}

void ai_fault_init(ai_fault_injector *injector) {
    if (!injector) return;
    injector->rule_count = 0;
}

bool ai_fault_configure(
    ai_fault_injector *injector,
    ai_fault_point point,
    ai_fault_mode mode,
    u32 every_n
) {
    if (!injector || !valid_point(point) || !valid_mode(mode)) return false;
    if (mode == AI_FAULT_EVERY_N && every_n == 0) return false;

    ai_fault_rule *rule = find_rule(injector, point);
    if (!rule) {
        if (injector->rule_count >= AI_FAULT_MAX_RULES) return false;
        rule = &injector->rules[injector->rule_count++];
    }

    rule->point = (u32)point;
    rule->mode = (u32)mode;
    rule->every_n = (mode == AI_FAULT_EVERY_N) ? every_n : 0;
    rule->seen = 0;
    rule->fired = 0;
    rule->enabled = true;
    return true;
}

void ai_fault_disable(ai_fault_injector *injector, ai_fault_point point) {
    ai_fault_rule *rule = find_rule(injector, point);
    if (rule) rule->enabled = false;
}

bool ai_fault_should_fail(ai_fault_injector *injector, ai_fault_point point) {
    ai_fault_rule *rule = find_rule(injector, point);
    if (!rule || !rule->enabled) return false;

    rule->seen++;
    bool fire = false;
    switch ((ai_fault_mode)rule->mode) {
        case AI_FAULT_ONCE:
            fire = (rule->fired == 0);
            break;
        case AI_FAULT_EVERY_N:
            fire = (rule->seen % rule->every_n) == 0;
            break;
        case AI_FAULT_ALWAYS:
            fire = true;
            break;
        case AI_FAULT_DISABLED:
        default:
            fire = false;
            break;
    }

    if (fire) rule->fired++;
    return fire;
}

u32 ai_fault_fired_count(const ai_fault_injector *injector, ai_fault_point point) {
    const ai_fault_rule *rule = find_rule_const(injector, point);
    return rule ? rule->fired : 0;
}
