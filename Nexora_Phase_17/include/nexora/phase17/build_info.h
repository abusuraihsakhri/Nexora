#ifndef NEXORA_PHASE17_BUILD_INFO_H
#define NEXORA_PHASE17_BUILD_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nx_build_info {
    const char *project;
    const char *phase;
    const char *version;
    const char *build_profile;
    const char *compiler;
    const char *build_date;
    const char *build_time;
} nx_build_info_t;

const nx_build_info_t *nx_build_info(void);

#ifdef __cplusplus
}
#endif

#endif
