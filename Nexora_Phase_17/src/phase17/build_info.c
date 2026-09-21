#include "nexora/phase17/build_info.h"

#ifndef NEXORA_VERSION
#define NEXORA_VERSION "0.17.0-rc1"
#endif

#ifndef NEXORA_BUILD_PROFILE
#define NEXORA_BUILD_PROFILE "development"
#endif

#ifndef NEXORA_BUILD_DATE
#define NEXORA_BUILD_DATE "unspecified"
#endif

#ifndef NEXORA_BUILD_TIME
#define NEXORA_BUILD_TIME "unspecified"
#endif

#if defined(__clang__)
#define NX_COMPILER "clang " __clang_version__
#elif defined(__GNUC__)
#define NX_COMPILER "gcc " __VERSION__
#elif defined(_MSC_VER)
#define NX_COMPILER "msvc"
#else
#define NX_COMPILER "unknown"
#endif

static const nx_build_info_t g_info = {
    .project = "Nexora",
    .phase = "17",
    .version = NEXORA_VERSION,
    .build_profile = NEXORA_BUILD_PROFILE,
    .compiler = NX_COMPILER,
    .build_date = NEXORA_BUILD_DATE,
    .build_time = NEXORA_BUILD_TIME,
};

const nx_build_info_t *nx_build_info(void) { return &g_info; }
