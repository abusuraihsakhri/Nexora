#include <ai/work.h>
#include <kernel/memory.h>
#include <kernel/panic.h>

void ai_work_graph_init(ai_work_graph *graph) {
    graph->node_count = 0;
    graph->next_id = 1;
}

ai_work_node *ai_work_add(
    ai_work_graph *graph,
    const char *name,
    ai_op op,
    u32 priority,
    u64 deadline_ns,
    u32 device_mask
) {
    if (graph->node_count >= AI_MAX_WORK_NODES) {
        panic("work graph full");
    }

    ai_work_node *node = (ai_work_node *)kalloc(sizeof(ai_work_node), 16);
    node->id = graph->next_id++;
    node->name = name;
    node->op = op;
    node->state = AI_WORK_PENDING;
    node->priority = priority;
    node->deadline_ns = deadline_ns;
    node->device_mask = device_mask;
    node->dependency_count = 0;
    node->input_count = 0;
    node->output_count = 0;

    graph->nodes[graph->node_count++] = node;
    return node;
}

void ai_work_add_dependency(ai_work_node *node, u64 dependency_id) {
    if (node->dependency_count >= AI_MAX_DEPS) {
        panic("too many work dependencies");
    }
    node->dependencies[node->dependency_count++] = dependency_id;
}

void ai_work_add_input(ai_work_node *node, ai_tensor *tensor) {
    if (node->input_count >= AI_MAX_INPUTS) {
        panic("too many work inputs");
    }
    node->inputs[node->input_count++] = tensor;
}

void ai_work_add_output(ai_work_node *node, ai_tensor *tensor) {
    if (node->output_count >= AI_MAX_OUTPUTS) {
        panic("too many work outputs");
    }
    node->outputs[node->output_count++] = tensor;
}

static const ai_work_node *find_node(const ai_work_graph *graph, u64 id) {
    for (u32 i = 0; i < graph->node_count; ++i) {
        if (graph->nodes[i]->id == id) {
            return graph->nodes[i];
        }
    }
    return NULL;
}

bool ai_work_dependencies_done(const ai_work_graph *graph, const ai_work_node *node) {
    for (u32 i = 0; i < node->dependency_count; ++i) {
        const ai_work_node *dep = find_node(graph, node->dependencies[i]);
        if (!dep || dep->state != AI_WORK_DONE) {
            return false;
        }
    }
    return true;
}

void ai_work_refresh_states(ai_work_graph *graph) {
    for (u32 i = 0; i < graph->node_count; ++i) {
        ai_work_node *node = graph->nodes[i];
        if (node->state == AI_WORK_PENDING &&
            ai_work_dependencies_done(graph, node)) {
            node->state = AI_WORK_READY;
        }
    }
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
        default: return "UNKNOWN";
    }
}
