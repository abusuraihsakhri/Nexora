#include <ai/work.h>
#include <kernel/memory.h>
#include <kernel/slab.h>
#include <kernel/panic.h>

void ai_work_graph_init(ai_work_graph *graph) {
    if (!graph) return;
    graph->node_count = 0;
    graph->next_id = 1;
    for (u32 i = 0; i < AI_MAX_WORK_NODES; ++i) {
        graph->nodes[i] = NULL;
    }
}

i32 ai_work_add_safe(
    ai_work_graph *graph,
    const char *name,
    ai_op op,
    u32 priority,
    u64 deadline_ns,
    u32 device_mask,
    ai_work_node **out_node
) {
    if (!out_node) return -1;
    *out_node = NULL;
    if (!graph || !name) return -2;
    if (graph->node_count >= AI_MAX_WORK_NODES) return -3;
    if (device_mask == 0) return -4;

    ai_work_node *node = NULL;
    if (ai_work_node_cache) {
        node = (ai_work_node *)kmem_cache_alloc(ai_work_node_cache);
    } else {
        node = (ai_work_node *)kalloc(sizeof(ai_work_node), 16);
    }
    if (!node) return -5;

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

    for (u32 i = 0; i < AI_MAX_DEPS; ++i) node->dependencies[i] = 0;
    for (u32 i = 0; i < AI_MAX_INPUTS; ++i) node->inputs[i] = NULL;
    for (u32 i = 0; i < AI_MAX_OUTPUTS; ++i) node->outputs[i] = NULL;

    graph->nodes[graph->node_count++] = node;
    *out_node = node;
    return 0;
}

ai_work_node *ai_work_add(
    ai_work_graph *graph,
    const char *name,
    ai_op op,
    u32 priority,
    u64 deadline_ns,
    u32 device_mask
) {
    ai_work_node *node = NULL;
    i32 rc = ai_work_add_safe(graph, name, op, priority, deadline_ns, device_mask, &node);
    if (rc != 0) {
        panic("ai_work_add: invalid argument or capacity exceeded");
    }
    return node;
}

i32 ai_work_add_dependency_safe(ai_work_node *node, u64 dependency_id) {
    if (!node || dependency_id == 0) return -1;
    if (node->dependency_count >= AI_MAX_DEPS) return -2;
    node->dependencies[node->dependency_count++] = dependency_id;
    return 0;
}

void ai_work_add_dependency(ai_work_node *node, u64 dependency_id) {
    if (ai_work_add_dependency_safe(node, dependency_id) != 0) {
        panic("ai_work_add_dependency failed");
    }
}

i32 ai_work_add_input_safe(ai_work_node *node, ai_tensor *tensor) {
    if (!node || !tensor) return -1;
    if (node->input_count >= AI_MAX_INPUTS) return -2;
    node->inputs[node->input_count++] = tensor;
    return 0;
}

void ai_work_add_input(ai_work_node *node, ai_tensor *tensor) {
    if (ai_work_add_input_safe(node, tensor) != 0) {
        panic("ai_work_add_input failed");
    }
}

i32 ai_work_add_output_safe(ai_work_node *node, ai_tensor *tensor) {
    if (!node || !tensor) return -1;
    if (node->output_count >= AI_MAX_OUTPUTS) return -2;
    node->outputs[node->output_count++] = tensor;
    return 0;
}

void ai_work_add_output(ai_work_node *node, ai_tensor *tensor) {
    if (ai_work_add_output_safe(node, tensor) != 0) {
        panic("ai_work_add_output failed");
    }
}

static const ai_work_node *find_node(const ai_work_graph *graph, u64 id) {
    if (!graph || id == 0) return NULL;
    for (u32 i = 0; i < graph->node_count; ++i) {
        if (graph->nodes[i] && graph->nodes[i]->id == id) {
            return graph->nodes[i];
        }
    }
    return NULL;
}

static i32 find_node_index(const ai_work_graph *graph, u64 id) {
    if (!graph || id == 0) return -1;
    for (u32 i = 0; i < graph->node_count; ++i) {
        if (graph->nodes[i] && graph->nodes[i]->id == id) {
            return (i32)i;
        }
    }
    return -1;
}

bool ai_work_dependencies_done(const ai_work_graph *graph, const ai_work_node *node) {
    if (!graph || !node) return false;
    for (u32 i = 0; i < node->dependency_count; ++i) {
        const ai_work_node *dep = find_node(graph, node->dependencies[i]);
        if (!dep || dep->state != AI_WORK_DONE) {
            return false;
        }
    }
    return true;
}

void ai_work_refresh_states(ai_work_graph *graph) {
    if (!graph) return;
    for (u32 i = 0; i < graph->node_count; ++i) {
        ai_work_node *node = graph->nodes[i];
        if (node && node->state == AI_WORK_PENDING &&
            ai_work_dependencies_done(graph, node)) {
            node->state = AI_WORK_READY;
        }
    }
}

u32 ai_work_graph_done_count(const ai_work_graph *graph) {
    if (!graph) return 0;
    u32 done = 0;
    for (u32 i = 0; i < graph->node_count; ++i) {
        if (graph->nodes[i] && graph->nodes[i]->state == AI_WORK_DONE) done++;
    }
    return done;
}

bool ai_work_graph_validate(const ai_work_graph *graph, ai_graph_validation *result) {
    ai_graph_validation local;
    ai_graph_validation *r = result ? result : &local;

    r->valid = false;
    r->node_count = 0;
    r->edge_count = 0;
    r->null_nodes = 0;
    r->duplicate_ids = 0;
    r->missing_dependencies = 0;
    r->self_dependencies = 0;
    r->duplicate_dependencies = 0;
    r->null_tensors = 0;
    r->invalid_device_masks = 0;
    r->cycle_nodes = 0;

    if (!graph || graph->node_count > AI_MAX_WORK_NODES) return false;
    r->node_count = graph->node_count;

    u32 indegree[AI_MAX_WORK_NODES];
    bool removed[AI_MAX_WORK_NODES];
    for (u32 i = 0; i < AI_MAX_WORK_NODES; ++i) {
        indegree[i] = 0;
        removed[i] = false;
    }

    for (u32 i = 0; i < graph->node_count; ++i) {
        const ai_work_node *node = graph->nodes[i];
        if (!node) {
            r->null_nodes++;
            continue;
        }
        if (node->device_mask == 0) r->invalid_device_masks++;

        for (u32 j = i + 1; j < graph->node_count; ++j) {
            const ai_work_node *other = graph->nodes[j];
            if (other && other->id == node->id) r->duplicate_ids++;
        }

        for (u32 j = 0; j < node->input_count; ++j) {
            if (!node->inputs[j] || !ai_tensor_validate(node->inputs[j])) r->null_tensors++;
        }
        for (u32 j = 0; j < node->output_count; ++j) {
            if (!node->outputs[j] || !ai_tensor_validate(node->outputs[j])) r->null_tensors++;
        }

        for (u32 d = 0; d < node->dependency_count; ++d) {
            u64 dep_id = node->dependencies[d];
            r->edge_count++;
            if (dep_id == node->id) r->self_dependencies++;
            for (u32 previous = 0; previous < d; ++previous) {
                if (node->dependencies[previous] == dep_id) {
                    r->duplicate_dependencies++;
                    break;
                }
            }
            if (find_node_index(graph, dep_id) < 0) {
                r->missing_dependencies++;
            } else {
                indegree[i]++;
            }
        }
    }

    if (r->null_nodes || r->duplicate_ids || r->missing_dependencies ||
        r->self_dependencies || r->duplicate_dependencies || r->null_tensors ||
        r->invalid_device_masks) {
        return false;
    }

    u32 removed_count = 0;
    bool progress = true;
    while (removed_count < graph->node_count && progress) {
        progress = false;
        for (u32 i = 0; i < graph->node_count; ++i) {
            if (removed[i] || indegree[i] != 0) continue;
            removed[i] = true;
            removed_count++;
            progress = true;

            u64 removed_id = graph->nodes[i]->id;
            for (u32 j = 0; j < graph->node_count; ++j) {
                if (removed[j] || indegree[j] == 0) continue;
                const ai_work_node *candidate = graph->nodes[j];
                for (u32 d = 0; d < candidate->dependency_count; ++d) {
                    if (candidate->dependencies[d] == removed_id) {
                        indegree[j]--;
                    }
                }
            }
        }
    }

    if (removed_count != graph->node_count) {
        r->cycle_nodes = graph->node_count - removed_count;
        return false;
    }

    r->valid = true;
    return true;
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

void ai_work_node_destroy(ai_work_graph *graph, ai_work_node *node) {
    if (!node) return;
    if (graph) {
        for (u32 i = 0; i < graph->node_count; ++i) {
            if (graph->nodes[i] == node) {
                for (u32 j = i; j + 1 < graph->node_count; ++j) {
                    graph->nodes[j] = graph->nodes[j + 1];
                }
                graph->nodes[--graph->node_count] = NULL;
                break;
            }
        }
    }
    if (ai_work_node_cache) {
        kmem_cache_free(ai_work_node_cache, node);
    }
}

void ai_work_graph_destroy(ai_work_graph *graph) {
    if (!graph) return;
    for (u32 i = 0; i < graph->node_count; ++i) {
        if (graph->nodes[i]) {
            if (ai_work_node_cache) {
                kmem_cache_free(ai_work_node_cache, graph->nodes[i]);
            }
            graph->nodes[i] = NULL;
        }
    }
    graph->node_count = 0;
}
