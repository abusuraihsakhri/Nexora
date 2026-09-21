#include <ai/capability.h>

ai_capability ai_cap_create(u64 subject_id, u64 rights) {
    ai_capability cap;
    cap.subject_id = subject_id;
    cap.rights = rights;
    return cap;
}

bool ai_cap_has(const ai_capability *cap, u64 requested) {
    if (!cap) return false;
    return (cap->rights & requested) == requested;
}
