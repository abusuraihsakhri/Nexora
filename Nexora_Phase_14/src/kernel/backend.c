#include <nexora/backend.h>
#include <stddef.h>

static const struct nexora_backend_ops *installed_ops;

void nexora_backend_install(const struct nexora_backend_ops *ops) {
    installed_ops = ops;
}

const struct nexora_backend_ops *nexora_backend_get(void) {
    return installed_ops;
}
