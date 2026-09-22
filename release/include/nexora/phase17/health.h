#ifndef NEXORA_PHASE17_HEALTH_H
#define NEXORA_PHASE17_HEALTH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NX_HEALTH_MAX_COMPONENTS 64u
#define NX_HEALTH_NAME_MAX 31u

typedef enum nx_health_state {
    NX_HEALTH_UNKNOWN = 0,
    NX_HEALTH_OK = 1,
    NX_HEALTH_DEGRADED = 2,
    NX_HEALTH_FAILED = 3
} nx_health_state_t;

typedef struct nx_health_snapshot {
    uint32_t id;
    char name[NX_HEALTH_NAME_MAX + 1u];
    nx_health_state_t state;
    uint64_t checks;
    uint64_t failures;
    uint64_t last_update_ns;
    int32_t last_code;
} nx_health_snapshot_t;

typedef struct nx_health_summary {
    uint32_t total;
    uint32_t ok;
    uint32_t degraded;
    uint32_t failed;
    uint32_t unknown;
    nx_health_state_t aggregate;
} nx_health_summary_t;

void nx_health_reset(void);
int nx_health_register(const char *name, uint32_t *out_id);
int nx_health_update(uint32_t id, nx_health_state_t state, int32_t code, uint64_t now_ns);
int nx_health_get(uint32_t id, nx_health_snapshot_t *out);
size_t nx_health_snapshot_all(nx_health_snapshot_t *out, size_t capacity);
nx_health_summary_t nx_health_summarize(void);
const char *nx_health_state_string(nx_health_state_t state);

#ifdef __cplusplus
}
#endif

#endif
