#ifndef AIKERNEL_BACKEND_BRIDGE_H
#define AIKERNEL_BACKEND_BRIDGE_H

#include <nexora/backend.h>

void ai_backend_bridge_install(void);
const struct nexora_backend_ops *ai_backend_bridge_ops(void);

#endif
