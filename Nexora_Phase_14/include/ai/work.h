#ifndef AIKERNEL_AI_WORK_H
#define AIKERNEL_AI_WORK_H

#include <kernel/types.h>
#include <ai/tensor.h>
#include <ai/device.h>

#define AI_MAX_WORK_NODES 64
#define AI_MAX_DEPS        8
#define AI_MAX_INPUTS      8
#define AI_MAX_OUTPUTS     4

typedef enum {
    AI_OP_NOOP,
    AI_OP_MATMUL,
    AI_OP_ATTENTION,
    AI_OP_ACTIVATION,
    AI_OP_NORMALIZATION,
    AI_OP_EMBEDDING,
    AI_OP_TRANSFER,
    AI_OP_CUSTOM
} ai_op;

typedef enum {
    AI_WORK_PENDING,
    AI_WORK_READY,
    AI_WORK_RUNNING,
    AI_WORK_DONE,
    AI_WORK_FAILED
} ai_work_state;

typedef struct ai_work_node {
    u64 id;
    const char *name;
    ai_op op;
    ai_work_state state;

    u32 priority;
    u64 deadline_ns;
    u32 device_mask;
    u32 refcount;

    u64 dependencies[AI_MAX_DEPS];
    u32 dependency_count;

    ai_tensor *inputs[AI_MAX_INPUTS];
    u32 input_count;

    ai_tensor *outputs[AI_MAX_OUTPUTS];
    u32 output_count;
} ai_work_node;

typedef struct {
    ai_work_node *nodes[AI_MAX_WORK_NODES];
    u32 node_count;
    u64 next_id;
} ai_work_graph;

typedef struct {
    bool valid;
    u32 node_count;
    u32 edge_count;
    u32 null_nodes;
    u32 duplicate_ids;
    u32 missing_dependencies;
    u32 self_dependencies;
    u32 duplicate_dependencies;
    u32 null_tensors;
    u32 invalid_device_masks;
    u32 cycle_nodes;
} ai_graph_validation;

void ai_work_graph_init(ai_work_graph *graph);

i32 ai_work_add_safe(
    ai_work_graph *graph,
    const char *name,
    ai_op op,
    u32 priority,
    u64 deadline_ns,
    u32 device_mask,
    ai_work_node **out_node
);

ai_work_node *ai_work_add(
    ai_work_graph *graph,
    const char *name,
    ai_op op,
    u32 priority,
    u64 deadline_ns,
    u32 device_mask
);

i32 ai_work_add_dependency_safe(ai_work_node *node, u64 dependency_id);
i32 ai_work_add_input_safe(ai_work_node *node, ai_tensor *tensor);
i32 ai_work_add_output_safe(ai_work_node *node, ai_tensor *tensor);

void ai_work_add_dependency(ai_work_node *node, u64 dependency_id);
void ai_work_add_input(ai_work_node *node, ai_tensor *tensor);
void ai_work_add_output(ai_work_node *node, ai_tensor *tensor);

bool ai_work_dependencies_done(const ai_work_graph *graph, const ai_work_node *node);
void ai_work_refresh_states(ai_work_graph *graph);
bool ai_work_graph_validate(const ai_work_graph *graph, ai_graph_validation *result);
u32 ai_work_graph_done_count(const ai_work_graph *graph);
bool ai_work_node_retain(ai_work_node *node);
void ai_work_node_destroy(ai_work_graph *graph, ai_work_node *node);
void ai_work_graph_destroy(ai_work_graph *graph);
const char *ai_work_state_name(ai_work_state state);
const char *ai_op_name(ai_op op);

#endif
