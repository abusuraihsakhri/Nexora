#include <ai/work.h>
#include <ai/lifetime.h>
#include <ai/reclaim.h>
#include <ai/instrument.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

static bool checked_add_u64(u64 a, u64 b, u64 *out) {
    if (out == NULL || a > (~(u64)0) - b) {
        return false;
    }
    *out = a + b;
    return true;
}

static u64 saturating_add_u64(u64 a, u64 b) {
    u64 out = 0;
    return checked_add_u64(a, b, &out) ? out : ~(u64)0;
}

static bool valid_op(ai_op op) {
    return op >= AI_OP_NOOP && op < AI_OP_COUNT;
}

static bool valid_work_class(ai_work_class work_class) {
    return work_class >= AI_WORK_CLASS_COMPUTE && work_class < AI_WORK_CLASS_COUNT;
}

static bool valid_qos(ai_work_qos qos) {
    return qos >= AI_WORK_QOS_DEFAULT && qos < AI_WORK_QOS_COUNT;
}

static const ai_work_node *find_node(const ai_work_graph *graph, u64 id) {
    if (graph == NULL) {
        return NULL;
    }

    for (u32 i = 0; i < graph->node_count; ++i) {
        if (graph->nodes[i] != NULL && graph->nodes[i]->object.id == id) {
            return graph->nodes[i];
        }
    }
    return NULL;
}

static i32 tensor_consumer_index(const ai_tensor *tensor, nx_handle_t handle) {
    if (tensor == NULL || handle == NX_INVALID_HANDLE) {
        return -1;
    }

    for (u32 i = 0; i < tensor->consumer_count; ++i) {
        if (tensor->consumers[i] == handle) {
            return (i32)i;
        }
    }
    return -1;
}

static bool tensor_has_input_pointer(const ai_work_node *node, const ai_tensor *tensor) {
    for (u32 i = 0; i < node->input_count; ++i) {
        if (node->inputs[i] == tensor) {
            return true;
        }
    }
    return false;
}

static bool tensor_has_output_pointer(const ai_work_node *node, const ai_tensor *tensor) {
    for (u32 i = 0; i < node->output_count; ++i) {
        if (node->outputs[i] == tensor) {
            return true;
        }
    }
    return false;
}

static ai_work_status tensor_add_consumer(ai_tensor *tensor, nx_handle_t handle) {
    if (tensor == NULL || handle == NX_INVALID_HANDLE) {
        return AI_WORK_ERR_INVALID_ARGUMENT;
    }
    if (tensor_consumer_index(tensor, handle) >= 0) {
        return AI_WORK_ERR_DUPLICATE_INPUT;
    }
    if (tensor->consumer_count >= AI_MAX_TENSOR_CONSUMERS) {
        return AI_WORK_ERR_CONSUMERS_FULL;
    }

    const u32 index = tensor->consumer_count;
    tensor->consumers[index] = handle;
    tensor->consumer_done_mask &= ~((u64)1u << index);
    ++tensor->consumer_count;
    ++tensor->consumers_remaining;
    return AI_WORK_OK;
}

static void tensor_remove_consumer(ai_tensor *tensor, nx_handle_t handle) {
    const i32 index_i32 = tensor_consumer_index(tensor, handle);
    if (index_i32 < 0) {
        return;
    }

    const u32 index = (u32)index_i32;
    const bool completed = (tensor->consumer_done_mask & ((u64)1u << index)) != 0;
    if (!completed && tensor->consumers_remaining > 0) {
        --tensor->consumers_remaining;
    }

    u64 new_mask = 0;
    for (u32 old = 0, next = 0; old < tensor->consumer_count; ++old) {
        if (old == index) {
            continue;
        }
        tensor->consumers[next] = tensor->consumers[old];
        if (tensor->consumer_done_mask & ((u64)1u << old)) {
            new_mask |= ((u64)1u << next);
        }
        ++next;
    }

    --tensor->consumer_count;
    tensor->consumers[tensor->consumer_count] = NX_INVALID_HANDLE;
    tensor->consumer_done_mask = new_mask;
}

static ai_work_status tensor_mark_consumer_done(
    ai_tensor *tensor,
    nx_handle_t handle,
    bool *out_final
) {
    if (out_final != NULL) {
        *out_final = false;
    }
    if (tensor == NULL || handle == NX_INVALID_HANDLE) {
        return AI_WORK_ERR_INVALID_ARGUMENT;
    }

    const i32 index_i32 = tensor_consumer_index(tensor, handle);
    if (index_i32 < 0) {
        return AI_WORK_ERR_CONSUMER_NOT_FOUND;
    }

    const u32 index = (u32)index_i32;
    const u64 bit = ((u64)1u << index);
    if (tensor->consumer_done_mask & bit) {
        return AI_WORK_ERR_CONSUMER_ALREADY_DONE;
    }

    tensor->consumer_done_mask |= bit;
    if (tensor->consumers_remaining > 0) {
        --tensor->consumers_remaining;
    }

    if (out_final != NULL && tensor->consumers_remaining == 0) {
        *out_final = true;
    }
    return AI_WORK_OK;
}

static void ai_work_object_destroy(nx_object *object) {
    ai_work_node *node = (ai_work_node *)object;

    /* Remove non-owning tensor-side graph metadata before dropping refs. */
    for (u32 i = 0; i < node->input_count; ++i) {
        ai_tensor *tensor = node->inputs[i];
        if (tensor != NULL) {
            tensor_remove_consumer(tensor, node->object.handle);
            (void)ai_tensor_release(tensor);
            node->inputs[i] = NULL;
        }
    }
    node->input_count = 0;

    for (u32 i = 0; i < node->output_count; ++i) {
        ai_tensor *tensor = node->outputs[i];
        if (tensor != NULL) {
            if (tensor->producer_work == node->object.handle) {
                tensor->producer_work = NX_INVALID_HANDLE;
            }
            (void)ai_tensor_release(tensor);
            node->outputs[i] = NULL;
        }
    }
    node->output_count = 0;
}

void ai_work_graph_init(ai_work_graph *graph) {
    if (graph == NULL) {
        return;
    }

    graph->node_count = 0;
    for (u32 i = 0; i < AI_MAX_WORK_NODES; ++i) {
        graph->nodes[i] = NULL;
    }

    graph->stats.nodes_created = 0;
    graph->stats.producer_links = 0;
    graph->stats.consumer_links = 0;
    graph->stats.consumer_completions = 0;
    graph->stats.final_consumer_events = 0;
    graph->stats.automatic_reclaim_attempts = 0;
    graph->stats.automatic_reclaim_successes = 0;
    graph->stats.automatic_reclaim_deferred = 0;
}

void ai_work_graph_destroy(ai_work_graph *graph) {
    if (graph == NULL) {
        return;
    }

    /*
     * Release in reverse construction order. The graph owns the creator
     * reference of each work object. Tensor edges are released by destructors.
     */
    while (graph->node_count > 0) {
        const u32 index = graph->node_count - 1;
        ai_work_node *node = graph->nodes[index];
        graph->nodes[index] = NULL;
        --graph->node_count;

        if (node != NULL && node->object.handle != NX_INVALID_HANDLE) {
            node->graph = NULL;
            (void)nx_object_release(node->object.handle);
        }
    }
}

const ai_work_graph_stats *ai_work_graph_get_stats(const ai_work_graph *graph) {
    return graph != NULL ? &graph->stats : NULL;
}

static ai_work_status validate_desc(const ai_work_desc *desc) {
    if (desc == NULL || desc->name == NULL) {
        return AI_WORK_ERR_INVALID_ARGUMENT;
    }
    if (!valid_op(desc->op)) {
        return AI_WORK_ERR_INVALID_OP;
    }
    if (!valid_work_class(desc->work_class)) {
        return AI_WORK_ERR_INVALID_CLASS;
    }
    if (!valid_qos(desc->qos)) {
        return AI_WORK_ERR_INVALID_QOS;
    }
    if (desc->priority > AI_WORK_PRIORITY_MAX) {
        return AI_WORK_ERR_INVALID_PRIORITY;
    }
    if (desc->device_mask == 0 ||
        (desc->device_mask & ~((u32)AI_WORK_VALID_DEVICE_MASK)) != 0) {
        return AI_WORK_ERR_INVALID_DEVICE_MASK;
    }
    if (desc->preferred_device != AI_DEVICE_INVALID) {
        ai_device *preferred = ai_device_lookup(desc->preferred_device);
        if (preferred == NULL || !ai_device_supports_mask(preferred, desc->device_mask)) {
            return AI_WORK_ERR_INVALID_PREFERRED_DEVICE;
        }
    }
    if ((desc->flags & ~((u32)AI_WORK_VALID_FLAGS)) != 0) {
        return AI_WORK_ERR_INVALID_FLAGS;
    }
    if (desc->batch_size == 0) {
        return AI_WORK_ERR_INVALID_BATCH;
    }
    if (desc->batch_id == AI_WORK_NO_BATCH && desc->batch_size != 1) {
        return AI_WORK_ERR_INVALID_BATCH;
    }
    if ((desc->flags & AI_WORK_FLAG_AUTO_IO_BYTES) != 0 &&
        (desc->estimated_read_bytes != 0 || desc->estimated_write_bytes != 0)) {
        return AI_WORK_ERR_INVALID_ESTIMATE;
    }
    return AI_WORK_OK;
}

ai_work_status ai_work_try_create(
    ai_work_graph *graph,
    const ai_work_desc *desc,
    ai_work_node **out_node
) {
    if (out_node != NULL) {
        *out_node = NULL;
    }
    if (graph == NULL || out_node == NULL) {
        return AI_WORK_ERR_INVALID_ARGUMENT;
    }

    ai_work_status status = validate_desc(desc);
    if (status != AI_WORK_OK) {
        return status;
    }
    if (graph->node_count >= AI_MAX_WORK_NODES) {
        return AI_WORK_ERR_GRAPH_FULL;
    }

    ai_work_node *node = (ai_work_node *)kalloc_try(sizeof(ai_work_node), 16);
    if (node == NULL) {
        return AI_WORK_ERR_OUT_OF_MEMORY;
    }

    if (!nx_object_register_ex(
            &node->object,
            NX_OBJECT_WORK,
            desc->name,
            NX_OBJECT_FLAG_NONE,
            NX_OWNER_KERNEL,
            ai_work_object_destroy)) {
        return AI_WORK_ERR_OBJECT_REGISTRY_FULL;
    }

    node->graph = graph;
    node->op = desc->op;
    node->work_class = desc->work_class;
    node->qos = desc->qos;
    node->state = AI_WORK_PENDING;

    node->priority = desc->priority;
    node->deadline_ns = desc->deadline_ns;
    node->device_mask = desc->device_mask;
    node->preferred_device = desc->preferred_device;
    node->selected_device = AI_DEVICE_INVALID;
    node->flags = desc->flags;

    node->estimated_ops = desc->estimated_ops;
    node->estimated_read_bytes = desc->estimated_read_bytes;
    node->estimated_write_bytes = desc->estimated_write_bytes;
    node->estimated_scratch_bytes = desc->estimated_scratch_bytes;
    node->estimated_duration_ns = desc->estimated_duration_ns;

    node->logical_input_bytes = 0;
    node->logical_output_bytes = 0;

    node->batch_id = desc->batch_id;
    node->batch_size = desc->batch_size;

    node->dependency_count = 0;
    node->input_count = 0;
    node->output_count = 0;

    for (u32 i = 0; i < AI_MAX_DEPS; ++i) {
        node->dependencies[i] = 0;
    }
    for (u32 i = 0; i < AI_MAX_INPUTS; ++i) {
        node->inputs[i] = NULL;
    }
    for (u32 i = 0; i < AI_MAX_OUTPUTS; ++i) {
        node->outputs[i] = NULL;
    }

    graph->nodes[graph->node_count++] = node;
    ++graph->stats.nodes_created;
    ai_trace_record(AI_TRACE_WORK_CREATE, node->object.id, 0, (u64)node->op);
    *out_node = node;
    return AI_WORK_OK;
}

ai_work_node *ai_work_create(ai_work_graph *graph, const ai_work_desc *desc) {
    ai_work_node *node = NULL;
    const ai_work_status status = ai_work_try_create(graph, desc, &node);
    if (status != AI_WORK_OK) {
        panic(ai_work_status_name(status));
    }
    return node;
}

ai_work_node *ai_work_add(
    ai_work_graph *graph,
    const char *name,
    ai_op op,
    u32 priority,
    u64 deadline_ns,
    u32 device_mask
) {
    ai_work_desc desc;
    desc.name = name;
    desc.op = op;
    desc.work_class = (op == AI_OP_TRANSFER)
        ? AI_WORK_CLASS_TRANSFER
        : ((op == AI_OP_NOOP) ? AI_WORK_CLASS_CONTROL : AI_WORK_CLASS_COMPUTE);
    desc.qos = AI_WORK_QOS_DEFAULT;
    desc.priority = priority;
    desc.deadline_ns = deadline_ns;
    desc.device_mask = device_mask;
    desc.preferred_device = AI_DEVICE_INVALID;
    desc.estimated_ops = 0;
    desc.estimated_read_bytes = 0;
    desc.estimated_write_bytes = 0;
    desc.estimated_scratch_bytes = 0;
    desc.estimated_duration_ns = 0;
    desc.batch_id = AI_WORK_NO_BATCH;
    desc.batch_size = 1;
    desc.flags = AI_WORK_FLAG_AUTO_IO_BYTES;
    return ai_work_create(graph, &desc);
}

ai_work_status ai_work_set_preferred_device(
    ai_work_node *node,
    ai_device_id_t device_id
) {
    if (node == NULL || node->state != AI_WORK_PENDING) {
        return AI_WORK_ERR_INVALID_STATE;
    }
    if (device_id == AI_DEVICE_INVALID) {
        node->preferred_device = AI_DEVICE_INVALID;
        return AI_WORK_OK;
    }

    ai_device *device = ai_device_lookup(device_id);
    if (device == NULL || !ai_device_supports_mask(device, node->device_mask)) {
        return AI_WORK_ERR_INVALID_PREFERRED_DEVICE;
    }
    node->preferred_device = device_id;
    return AI_WORK_OK;
}

ai_work_status ai_work_try_add_dependency(ai_work_node *node, u64 dependency_id) {
    if (node == NULL || dependency_id == 0) {
        return AI_WORK_ERR_INVALID_ARGUMENT;
    }
    if (node->state != AI_WORK_PENDING) {
        return AI_WORK_ERR_INVALID_STATE;
    }
    if (node->object.id == dependency_id) {
        return AI_WORK_ERR_SELF_DEPENDENCY;
    }

    for (u32 i = 0; i < node->dependency_count; ++i) {
        if (node->dependencies[i] == dependency_id) {
            return AI_WORK_ERR_DUPLICATE_DEPENDENCY;
        }
    }
    if (node->dependency_count >= AI_MAX_DEPS) {
        return AI_WORK_ERR_DEPENDENCIES_FULL;
    }

    node->dependencies[node->dependency_count++] = dependency_id;
    return AI_WORK_OK;
}

ai_work_status ai_work_try_add_input(ai_work_node *node, ai_tensor *tensor) {
    if (node == NULL || tensor == NULL || node->graph == NULL ||
        node->object.state != NX_OBJECT_LIVE || tensor->object.state != NX_OBJECT_LIVE) {
        return AI_WORK_ERR_INVALID_ARGUMENT;
    }
    if (node->state != AI_WORK_PENDING) {
        return AI_WORK_ERR_INVALID_STATE;
    }
    if (node->input_count >= AI_MAX_INPUTS) {
        return AI_WORK_ERR_INPUTS_FULL;
    }
    if (tensor_has_input_pointer(node, tensor)) {
        return AI_WORK_ERR_DUPLICATE_INPUT;
    }
    if (tensor->producer_work == node->object.handle) {
        return AI_WORK_ERR_SELF_DEPENDENCY;
    }

    u64 new_logical_input_bytes = 0;
    if (!checked_add_u64(node->logical_input_bytes, tensor->logical_bytes,
                         &new_logical_input_bytes)) {
        return AI_WORK_ERR_ESTIMATE_OVERFLOW;
    }

    ai_work_status status = tensor_add_consumer(tensor, node->object.handle);
    if (status != AI_WORK_OK) {
        return status;
    }

    if (!ai_tensor_retain(tensor)) {
        tensor_remove_consumer(tensor, node->object.handle);
        return AI_WORK_ERR_TENSOR_RETAIN_FAILED;
    }

    node->inputs[node->input_count++] = tensor;
    node->logical_input_bytes = new_logical_input_bytes;
    if ((node->flags & AI_WORK_FLAG_AUTO_IO_BYTES) != 0) {
        node->estimated_read_bytes = node->logical_input_bytes;
    }
    ++node->graph->stats.consumer_links;
    return AI_WORK_OK;
}

ai_work_status ai_work_try_add_output(ai_work_node *node, ai_tensor *tensor) {
    if (node == NULL || tensor == NULL || node->graph == NULL ||
        node->object.state != NX_OBJECT_LIVE || tensor->object.state != NX_OBJECT_LIVE) {
        return AI_WORK_ERR_INVALID_ARGUMENT;
    }
    if (node->state != AI_WORK_PENDING) {
        return AI_WORK_ERR_INVALID_STATE;
    }
    if (node->output_count >= AI_MAX_OUTPUTS) {
        return AI_WORK_ERR_OUTPUTS_FULL;
    }
    if (tensor_has_output_pointer(node, tensor)) {
        return AI_WORK_ERR_DUPLICATE_OUTPUT;
    }
    if (tensor_has_input_pointer(node, tensor)) {
        return AI_WORK_ERR_SELF_DEPENDENCY;
    }
    if (tensor->producer_work != NX_INVALID_HANDLE) {
        return AI_WORK_ERR_PRODUCER_EXISTS;
    }

    u64 new_logical_output_bytes = 0;
    if (!checked_add_u64(node->logical_output_bytes, tensor->logical_bytes,
                         &new_logical_output_bytes)) {
        return AI_WORK_ERR_ESTIMATE_OVERFLOW;
    }

    if (!ai_tensor_retain(tensor)) {
        return AI_WORK_ERR_TENSOR_RETAIN_FAILED;
    }

    tensor->producer_work = node->object.handle;
    node->outputs[node->output_count++] = tensor;
    node->logical_output_bytes = new_logical_output_bytes;
    if ((node->flags & AI_WORK_FLAG_AUTO_IO_BYTES) != 0) {
        node->estimated_write_bytes = node->logical_output_bytes;
    }
    ++node->graph->stats.producer_links;
    return AI_WORK_OK;
}

void ai_work_add_dependency(ai_work_node *node, u64 dependency_id) {
    const ai_work_status status = ai_work_try_add_dependency(node, dependency_id);
    if (status != AI_WORK_OK) {
        panic(ai_work_status_name(status));
    }
}

void ai_work_add_input(ai_work_node *node, ai_tensor *tensor) {
    const ai_work_status status = ai_work_try_add_input(node, tensor);
    if (status != AI_WORK_OK) {
        panic(ai_work_status_name(status));
    }
}

void ai_work_add_output(ai_work_node *node, ai_tensor *tensor) {
    const ai_work_status status = ai_work_try_add_output(node, tensor);
    if (status != AI_WORK_OK) {
        panic(ai_work_status_name(status));
    }
}

bool ai_work_dependencies_done(const ai_work_graph *graph, const ai_work_node *node) {
    if (graph == NULL || node == NULL) {
        return false;
    }

    for (u32 i = 0; i < node->dependency_count; ++i) {
        const ai_work_node *dep = find_node(graph, node->dependencies[i]);
        if (dep == NULL || dep->state != AI_WORK_DONE) {
            return false;
        }
    }

    /* Tensor producer relationships are data dependencies automatically. */
    for (u32 i = 0; i < node->input_count; ++i) {
        const ai_tensor *tensor = node->inputs[i];
        if (tensor == NULL || tensor->producer_work == NX_INVALID_HANDLE) {
            continue;
        }

        nx_object *producer_object = nx_object_lookup(tensor->producer_work, NX_OBJECT_WORK);
        if (producer_object == NULL) {
            return false;
        }

        const ai_work_node *producer = (const ai_work_node *)producer_object;
        if (producer == node || producer->state != AI_WORK_DONE) {
            return false;
        }
    }

    return true;
}

void ai_work_refresh_states(ai_work_graph *graph) {
    if (graph == NULL) {
        return;
    }

    for (u32 i = 0; i < graph->node_count; ++i) {
        ai_work_node *node = graph->nodes[i];
        if (node != NULL && node->state == AI_WORK_PENDING &&
            ai_work_dependencies_done(graph, node)) {
            node->state = AI_WORK_READY;
        }
    }
}

ai_work_status ai_work_complete(ai_work_node *node) {
    if (node == NULL || node->graph == NULL) {
        return AI_WORK_ERR_INVALID_ARGUMENT;
    }
    if (node->state != AI_WORK_RUNNING && node->state != AI_WORK_READY) {
        return AI_WORK_ERR_INVALID_STATE;
    }

    /* Preflight all input edges so completion is all-or-nothing. */
    for (u32 i = 0; i < node->input_count; ++i) {
        ai_tensor *tensor = node->inputs[i];
        const i32 index_i32 = tensor_consumer_index(tensor, node->object.handle);
        if (index_i32 < 0) {
            return AI_WORK_ERR_CONSUMER_NOT_FOUND;
        }
        if (tensor->consumer_done_mask & ((u64)1u << (u32)index_i32)) {
            return AI_WORK_ERR_CONSUMER_ALREADY_DONE;
        }
    }

    node->state = AI_WORK_DONE;
    ai_device *trace_device = ai_device_lookup(node->selected_device);
    ai_trace_record(
        AI_TRACE_WORK_COMPLETE,
        node->object.id,
        trace_device != NULL ? trace_device->object.id : 0,
        node->logical_input_bytes
    );

    for (u32 i = 0; i < node->input_count; ++i) {
        ai_tensor *tensor = node->inputs[i];
        bool final_consumer = false;
        const ai_work_status status = tensor_mark_consumer_done(
            tensor,
            node->object.handle,
            &final_consumer
        );

        if (status != AI_WORK_OK) {
            return status;
        }

        ++node->graph->stats.consumer_completions;

        if (!final_consumer) {
            continue;
        }

        ++node->graph->stats.final_consumer_events;

        if (tensor->lifetime == AI_TENSOR_LIFETIME_TEMPORARY &&
            ai_tensor_is_backed(tensor)) {
            ++node->graph->stats.automatic_reclaim_attempts;
            bool deferred = false;
            const ai_lifetime_status reclaim_status = ai_reclaim_on_final_consumer(
                tensor,
                &deferred
            );
            if (reclaim_status == AI_LIFETIME_OK) {
                ++node->graph->stats.automatic_reclaim_successes;
            } else if (deferred) {
                ++node->graph->stats.automatic_reclaim_deferred;
            }
        }
    }

    return AI_WORK_OK;
}

bool ai_work_has_deadline(const ai_work_node *node) {
    return node != NULL && node->deadline_ns != AI_WORK_NO_DEADLINE;
}

bool ai_work_is_profiled(const ai_work_node *node) {
    return node != NULL &&
        (node->estimated_ops != 0 ||
         node->estimated_read_bytes != 0 ||
         node->estimated_write_bytes != 0 ||
         node->estimated_scratch_bytes != 0 ||
         node->estimated_duration_ns != 0);
}

u64 ai_work_estimated_total_bytes(const ai_work_node *node) {
    if (node == NULL) {
        return 0;
    }
    u64 total = saturating_add_u64(node->estimated_read_bytes, node->estimated_write_bytes);
    return saturating_add_u64(total, node->estimated_scratch_bytes);
}

u64 ai_work_logical_total_bytes(const ai_work_node *node) {
    if (node == NULL) {
        return 0;
    }
    return saturating_add_u64(node->logical_input_bytes, node->logical_output_bytes);
}

nx_handle_t ai_tensor_producer_work(const ai_tensor *tensor) {
    return tensor != NULL ? tensor->producer_work : NX_INVALID_HANDLE;
}

u32 ai_tensor_consumer_count(const ai_tensor *tensor) {
    return tensor != NULL ? tensor->consumer_count : 0;
}

u32 ai_tensor_consumers_remaining(const ai_tensor *tensor) {
    return tensor != NULL ? tensor->consumers_remaining : 0;
}

bool ai_tensor_has_consumer(const ai_tensor *tensor, nx_handle_t work_handle) {
    return tensor_consumer_index(tensor, work_handle) >= 0;
}

bool ai_tensor_consumer_done(const ai_tensor *tensor, nx_handle_t work_handle) {
    const i32 index_i32 = tensor_consumer_index(tensor, work_handle);
    if (index_i32 < 0) {
        return false;
    }
    const u32 index = (u32)index_i32;
    return (tensor->consumer_done_mask & ((u64)1u << index)) != 0;
}

const char *ai_work_state_name(ai_work_state state) {
    switch (state) {
        case AI_WORK_PENDING: return "PENDING";
        case AI_WORK_READY: return "READY";
        case AI_WORK_RUNNING: return "RUNNING";
        case AI_WORK_DONE: return "DONE";
        case AI_WORK_FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

const char *ai_op_name(ai_op op) {
    switch (op) {
        case AI_OP_NOOP: return "NOOP";
        case AI_OP_MATMUL: return "MATMUL";
        case AI_OP_ATTENTION: return "ATTENTION";
        case AI_OP_ACTIVATION: return "ACTIVATION";
        case AI_OP_NORMALIZATION: return "NORMALIZATION";
        case AI_OP_EMBEDDING: return "EMBEDDING";
        case AI_OP_TRANSFER: return "TRANSFER";
        case AI_OP_CUSTOM: return "CUSTOM";
        case AI_OP_COUNT: break;
        default: break;
    }
    return "UNKNOWN";
}

const char *ai_work_class_name(ai_work_class work_class) {
    switch (work_class) {
        case AI_WORK_CLASS_COMPUTE: return "COMPUTE";
        case AI_WORK_CLASS_MEMORY: return "MEMORY";
        case AI_WORK_CLASS_TRANSFER: return "TRANSFER";
        case AI_WORK_CLASS_CONTROL: return "CONTROL";
        case AI_WORK_CLASS_CUSTOM: return "CUSTOM";
        case AI_WORK_CLASS_COUNT: break;
        default: break;
    }
    return "UNKNOWN";
}

const char *ai_work_qos_name(ai_work_qos qos) {
    switch (qos) {
        case AI_WORK_QOS_DEFAULT: return "DEFAULT";
        case AI_WORK_QOS_LATENCY: return "LATENCY";
        case AI_WORK_QOS_THROUGHPUT: return "THROUGHPUT";
        case AI_WORK_QOS_BACKGROUND: return "BACKGROUND";
        case AI_WORK_QOS_COUNT: break;
        default: break;
    }
    return "UNKNOWN";
}

const char *ai_work_status_name(ai_work_status status) {
    switch (status) {
        case AI_WORK_OK: return "work ok";
        case AI_WORK_ERR_INVALID_ARGUMENT: return "work invalid argument";
        case AI_WORK_ERR_GRAPH_FULL: return "work graph full";
        case AI_WORK_ERR_OUT_OF_MEMORY: return "work allocation failed";
        case AI_WORK_ERR_OBJECT_REGISTRY_FULL: return "work object registry full";
        case AI_WORK_ERR_INVALID_OP: return "invalid work operation";
        case AI_WORK_ERR_INVALID_CLASS: return "invalid work class";
        case AI_WORK_ERR_INVALID_QOS: return "invalid work qos";
        case AI_WORK_ERR_INVALID_PRIORITY: return "invalid work priority";
        case AI_WORK_ERR_INVALID_DEVICE_MASK: return "invalid work device mask";
        case AI_WORK_ERR_INVALID_PREFERRED_DEVICE: return "invalid preferred device";
        case AI_WORK_ERR_INVALID_FLAGS: return "invalid work flags";
        case AI_WORK_ERR_INVALID_BATCH: return "invalid work batch metadata";
        case AI_WORK_ERR_INVALID_ESTIMATE: return "invalid work estimate configuration";
        case AI_WORK_ERR_ESTIMATE_OVERFLOW: return "work byte estimate overflow";
        case AI_WORK_ERR_DEPENDENCIES_FULL: return "too many work dependencies";
        case AI_WORK_ERR_INPUTS_FULL: return "too many work inputs";
        case AI_WORK_ERR_OUTPUTS_FULL: return "too many work outputs";
        case AI_WORK_ERR_CONSUMERS_FULL: return "too many tensor consumers";
        case AI_WORK_ERR_DUPLICATE_INPUT: return "duplicate work input";
        case AI_WORK_ERR_DUPLICATE_OUTPUT: return "duplicate work output";
        case AI_WORK_ERR_DUPLICATE_DEPENDENCY: return "duplicate work dependency";
        case AI_WORK_ERR_PRODUCER_EXISTS: return "tensor already has producer";
        case AI_WORK_ERR_SELF_DEPENDENCY: return "work self dependency";
        case AI_WORK_ERR_TENSOR_RETAIN_FAILED: return "tensor retain failed";
        case AI_WORK_ERR_INVALID_STATE: return "invalid work state";
        case AI_WORK_ERR_CONSUMER_NOT_FOUND: return "tensor consumer not found";
        case AI_WORK_ERR_CONSUMER_ALREADY_DONE: return "tensor consumer already done";
        default: return "work unknown error";
    }
}
