#include <ai/backend_bridge.h>
#include <ai/tensor.h>
#include <ai/work.h>
#include <ai/scheduler.h>
#include <kernel/slab.h>
#include <kernel/types.h>
#include <nexora/process.h>
#include <nexora/handle.h>

static ai_work_graph g_unified_graph;
static bool g_graph_initialized = false;

static nexora_status_t ai_bridge_retain(uint8_t handle_type, void *object) {
    if (!object) return NEXORA_ERR(NEXORA_EINVAL);
    if (handle_type == NEXORA_HANDLE_TENSOR) {
        return ai_tensor_retain((ai_tensor *)object) ? NEXORA_OK : NEXORA_ERR(NEXORA_EOVERFLOW);
    }
    if (handle_type == NEXORA_HANDLE_WORK) {
        return ai_work_node_retain((ai_work_node *)object) ? NEXORA_OK : NEXORA_ERR(NEXORA_EOVERFLOW);
    }
    return NEXORA_ERR(NEXORA_EINVAL);
}

static nexora_status_t ai_bridge_tensor_create(
    struct nexora_process *process,
    const struct nexora_tensor_desc *desc,
    void **object_out,
    uint64_t *rights_out
) {
    (void)process;
    if (!desc || !object_out || !rights_out) return NEXORA_ERR(NEXORA_EINVAL);

    ai_tensor *t = NULL;
    i32 rc = ai_tensor_create_safe(
        "user_tensor",
        (ai_dtype)desc->dtype,
        desc->ndim,
        desc->shape,
        (ai_tensor_location)desc->location,
        desc->flags,
        &t
    );
    if (rc != 0 || !t) {
        return NEXORA_ERR(NEXORA_ENOMEM);
    }

    *object_out = t;
    *rights_out = NEXORA_RIGHT_READ | NEXORA_RIGHT_WRITE |
                  NEXORA_RIGHT_RELEASE | NEXORA_RIGHT_DELEGATE;
    return NEXORA_OK;
}

static nexora_status_t ai_bridge_tensor_release(
    struct nexora_process *process,
    void *tensor_object
) {
    (void)process;
    if (!tensor_object) return NEXORA_ERR(NEXORA_EINVAL);
    ai_tensor_destroy((ai_tensor *)tensor_object);
    return NEXORA_OK;
}

static nexora_status_t ai_bridge_work_submit(
    struct nexora_process *process,
    const struct nexora_work_desc *desc,
    void *const *input_objects,
    void *const *output_objects,
    void **work_object_out
) {
    (void)process;
    if (!desc || !work_object_out) return NEXORA_ERR(NEXORA_EINVAL);

    if (!g_graph_initialized) {
        ai_work_graph_init(&g_unified_graph);
        g_graph_initialized = true;
    }

    ai_work_node *node = NULL;
    i32 rc = ai_work_add_safe(
        &g_unified_graph,
        "user_work",
        (ai_op)desc->op,
        desc->priority,
        desc->deadline_ns,
        desc->device_mask,
        &node
    );
    if (rc != 0 || !node) {
        return NEXORA_ERR(NEXORA_ENOMEM);
    }

    /* Attach input and output tensors */
    if (input_objects) {
        for (u32 i = 0; i < desc->input_count; ++i) {
            if (input_objects[i]) {
                (void)ai_work_add_input_safe(node, (ai_tensor *)input_objects[i]);
            }
        }
    }
    if (output_objects) {
        for (u32 i = 0; i < desc->output_count; ++i) {
            if (output_objects[i]) {
                (void)ai_work_add_output_safe(node, (ai_tensor *)output_objects[i]);
            }
        }
    }

    *work_object_out = node;
    return NEXORA_OK;
}

static nexora_status_t ai_bridge_work_wait(
    struct nexora_process *process,
    void *work_object,
    uint64_t timeout_ns,
    struct nexora_work_result *result_out
) {
    (void)process;
    if (!work_object || !result_out) return NEXORA_ERR(NEXORA_EINVAL);

    ai_work_node *node = (ai_work_node *)work_object;

    if (node->state != AI_WORK_DONE && node->state != AI_WORK_FAILED) {
        if (timeout_ns == 0) return NEXORA_ERR(NEXORA_ETIMEDOUT);
        if (g_graph_initialized) {
            ai_scheduler scheduler;
            ai_scheduler_init(&scheduler, &g_unified_graph);
            ai_scheduler_run_report rep;
            (void)ai_scheduler_run_to_completion(&scheduler, &rep);
        }
    }

    result_out->struct_size = sizeof(*result_out);
    result_out->state = (uint32_t)node->state;
    result_out->device_id = 0;
    result_out->completion_code = node->state == AI_WORK_FAILED ? -NEXORA_EIO : 0;
    /* No monotonic kernel clock is wired yet; never fabricate timestamps. */
    result_out->started_ns = 0;
    result_out->completed_ns = 0;

    return NEXORA_OK;
}

static nexora_status_t ai_bridge_work_release(
    struct nexora_process *process,
    void *work_object
) {
    (void)process;
    if (!work_object) return NEXORA_ERR(NEXORA_EINVAL);
    if (g_graph_initialized) {
        ai_work_node_destroy(&g_unified_graph, (ai_work_node *)work_object);
    }
    return NEXORA_OK;
}

static nexora_status_t ai_bridge_device_query(
    struct nexora_process *process,
    uint32_t ordinal,
    struct nexora_device_info *info_out
) {
    (void)process;
    if (!info_out || ordinal != 0) return NEXORA_ERR(NEXORA_EINVAL);

    info_out->struct_size = sizeof(*info_out);
    info_out->id = 0;
    info_out->kind = NEXORA_DEVICE_CPU;
    info_out->numa_node = 0;
    info_out->memory_bytes = 1024 * 1024 * 1024ull;
    info_out->feature_bits = 0x7;
    const char dev_name[] = "Nexora-Unified-CPU";
    for (usize i = 0; i < sizeof(dev_name); ++i) {
        info_out->name[i] = dev_name[i];
    }
    return NEXORA_OK;
}

static const struct nexora_backend_ops s_ai_ops = {
    .retain = ai_bridge_retain,
    .tensor_create = ai_bridge_tensor_create,
    .tensor_map = NULL,
    .tensor_release = ai_bridge_tensor_release,
    .work_submit = ai_bridge_work_submit,
    .work_wait = ai_bridge_work_wait,
    .work_release = ai_bridge_work_release,
    .device_query = ai_bridge_device_query,
};

void ai_backend_bridge_install(void) {
    nexora_backend_install(&s_ai_ops);
}

const struct nexora_backend_ops *ai_backend_bridge_ops(void) {
    return &s_ai_ops;
}
