#ifndef AIKERNEL_AI_WORK_H
#define AIKERNEL_AI_WORK_H

#include <kernel/types.h>
#include <kernel/object.h>
#include <ai/tensor.h>
#include <ai/device.h>

#define AI_MAX_WORK_NODES 64
#define AI_MAX_DEPS        8
#define AI_MAX_INPUTS      8
#define AI_MAX_OUTPUTS     4

#define AI_WORK_PRIORITY_MAX 255u
#define AI_WORK_NO_DEADLINE  0ull
#define AI_WORK_NO_BATCH     0ull
#define AI_WORK_VALID_DEVICE_MASK \
    (AI_DEVICE_CPU | AI_DEVICE_GPU | AI_DEVICE_NPU | AI_DEVICE_NIC)

typedef enum {
    AI_OP_NOOP,
    AI_OP_MATMUL,
    AI_OP_ATTENTION,
    AI_OP_ACTIVATION,
    AI_OP_NORMALIZATION,
    AI_OP_EMBEDDING,
    AI_OP_TRANSFER,
    AI_OP_CUSTOM,
    AI_OP_COUNT
} ai_op;

typedef enum {
    AI_WORK_PENDING,
    AI_WORK_READY,
    AI_WORK_RUNNING,
    AI_WORK_DONE,
    AI_WORK_FAILED
} ai_work_state;

/*
 * Execution character of the work unit. This is intentionally orthogonal to
 * ai_op: two CUSTOM operations can still have very different scheduling and
 * placement behavior.
 */
typedef enum {
    AI_WORK_CLASS_COMPUTE = 0,
    AI_WORK_CLASS_MEMORY,
    AI_WORK_CLASS_TRANSFER,
    AI_WORK_CLASS_CONTROL,
    AI_WORK_CLASS_CUSTOM,
    AI_WORK_CLASS_COUNT
} ai_work_class;

/* Explicit service intent supplied by the submitter. */
typedef enum {
    AI_WORK_QOS_DEFAULT = 0,
    AI_WORK_QOS_LATENCY,
    AI_WORK_QOS_THROUGHPUT,
    AI_WORK_QOS_BACKGROUND,
    AI_WORK_QOS_COUNT
} ai_work_qos;

typedef enum {
    /* Derive estimated read/write traffic from attached tensor edges. */
    AI_WORK_FLAG_AUTO_IO_BYTES = 1u << 0,
    /* Future schedulers may preempt this work at a safe boundary. */
    AI_WORK_FLAG_PREEMPTIBLE   = 1u << 1,
    /* Work may be merged with compatible work from the same batch domain. */
    AI_WORK_FLAG_BATCHABLE     = 1u << 2,
    /* Same inputs/configuration are expected to produce deterministic output. */
    AI_WORK_FLAG_DETERMINISTIC = 1u << 3,
    /* Safe to retry after a failed dispatch without externally visible effects. */
    AI_WORK_FLAG_IDEMPOTENT    = 1u << 4
} ai_work_flags;

#define AI_WORK_VALID_FLAGS \
    (AI_WORK_FLAG_AUTO_IO_BYTES | AI_WORK_FLAG_PREEMPTIBLE | \
     AI_WORK_FLAG_BATCHABLE | AI_WORK_FLAG_DETERMINISTIC | \
     AI_WORK_FLAG_IDEMPOTENT)

typedef enum {
    AI_WORK_OK = 0,
    AI_WORK_ERR_INVALID_ARGUMENT,
    AI_WORK_ERR_GRAPH_FULL,
    AI_WORK_ERR_OUT_OF_MEMORY,
    AI_WORK_ERR_OBJECT_REGISTRY_FULL,
    AI_WORK_ERR_INVALID_OP,
    AI_WORK_ERR_INVALID_CLASS,
    AI_WORK_ERR_INVALID_QOS,
    AI_WORK_ERR_INVALID_PRIORITY,
    AI_WORK_ERR_INVALID_DEVICE_MASK,
    AI_WORK_ERR_INVALID_PREFERRED_DEVICE,
    AI_WORK_ERR_INVALID_FLAGS,
    AI_WORK_ERR_INVALID_BATCH,
    AI_WORK_ERR_INVALID_ESTIMATE,
    AI_WORK_ERR_ESTIMATE_OVERFLOW,
    AI_WORK_ERR_DEPENDENCIES_FULL,
    AI_WORK_ERR_INPUTS_FULL,
    AI_WORK_ERR_OUTPUTS_FULL,
    AI_WORK_ERR_CONSUMERS_FULL,
    AI_WORK_ERR_DUPLICATE_INPUT,
    AI_WORK_ERR_DUPLICATE_OUTPUT,
    AI_WORK_ERR_DUPLICATE_DEPENDENCY,
    AI_WORK_ERR_PRODUCER_EXISTS,
    AI_WORK_ERR_SELF_DEPENDENCY,
    AI_WORK_ERR_TENSOR_RETAIN_FAILED,
    AI_WORK_ERR_INVALID_STATE,
    AI_WORK_ERR_CONSUMER_NOT_FOUND,
    AI_WORK_ERR_CONSUMER_ALREADY_DONE
} ai_work_status;

/*
 * AI-native execution metadata. Values are estimates/hints, not accounting
 * claims. A value of zero means "unknown/not supplied" except batch_size,
 * which must be at least one.
 */
typedef struct {
    const char *name;
    ai_op op;
    ai_work_class work_class;
    ai_work_qos qos;

    u32 priority;
    u64 deadline_ns;       /* AI_WORK_NO_DEADLINE means no deadline. */
    u32 device_mask;       /* Allowed device classes. */
    ai_device_id_t preferred_device; /* Optional concrete device. */

    u64 estimated_ops;
    u64 estimated_read_bytes;
    u64 estimated_write_bytes;
    u64 estimated_scratch_bytes;
    u64 estimated_duration_ns;

    u64 batch_id;          /* AI_WORK_NO_BATCH means not assigned to a batch. */
    u32 batch_size;
    u32 flags;
} ai_work_desc;

typedef struct {
    u64 nodes_created;
    u64 producer_links;
    u64 consumer_links;
    u64 consumer_completions;
    u64 final_consumer_events;
    u64 automatic_reclaim_attempts;
    u64 automatic_reclaim_successes;
    u64 automatic_reclaim_deferred;
} ai_work_graph_stats;

struct ai_work_graph;

typedef struct ai_work_node {
    nx_object object;
    struct ai_work_graph *graph;

    ai_op op;
    ai_work_class work_class;
    ai_work_qos qos;
    ai_work_state state;

    u32 priority;
    u64 deadline_ns;
    u32 device_mask;
    ai_device_id_t preferred_device;
    ai_device_id_t selected_device;
    u32 flags;

    /* Kernel-visible cost model for future scheduling/placement policy. */
    u64 estimated_ops;
    u64 estimated_read_bytes;
    u64 estimated_write_bytes;
    u64 estimated_scratch_bytes;
    u64 estimated_duration_ns;

    /* Exact logical edge volume derived from attached tensor metadata. */
    u64 logical_input_bytes;
    u64 logical_output_bytes;

    /* Batch identity is metadata only in Step 7; batching policy comes later. */
    u64 batch_id;
    u32 batch_size;

    /* Explicit control dependencies, represented by global work-object IDs. */
    u64 dependencies[AI_MAX_DEPS];
    u32 dependency_count;

    /* Work nodes own one strong tensor reference for every attached edge. */
    ai_tensor *inputs[AI_MAX_INPUTS];
    u32 input_count;

    ai_tensor *outputs[AI_MAX_OUTPUTS];
    u32 output_count;
} ai_work_node;

typedef struct ai_work_graph {
    ai_work_node *nodes[AI_MAX_WORK_NODES];
    u32 node_count;
    ai_work_graph_stats stats;
} ai_work_graph;

void ai_work_graph_init(ai_work_graph *graph);
void ai_work_graph_destroy(ai_work_graph *graph);
const ai_work_graph_stats *ai_work_graph_get_stats(const ai_work_graph *graph);

/* Fallible descriptor-based constructor introduced in Phase 3 Step 7. */
ai_work_status ai_work_try_create(
    ai_work_graph *graph,
    const ai_work_desc *desc,
    ai_work_node **out_node
);

ai_work_node *ai_work_create(
    ai_work_graph *graph,
    const ai_work_desc *desc
);

/*
 * Backward-compatible constructor. It creates COMPUTE/DEFAULT work and derives
 * read/write byte estimates automatically from attached tensor edges.
 */
ai_work_node *ai_work_add(
    ai_work_graph *graph,
    const char *name,
    ai_op op,
    u32 priority,
    u64 deadline_ns,
    u32 device_mask
);

ai_work_status ai_work_set_preferred_device(
    ai_work_node *node,
    ai_device_id_t device_id
);

ai_work_status ai_work_try_add_dependency(ai_work_node *node, u64 dependency_id);
ai_work_status ai_work_try_add_input(ai_work_node *node, ai_tensor *tensor);
ai_work_status ai_work_try_add_output(ai_work_node *node, ai_tensor *tensor);

/* Panic-on-programmer-error compatibility wrappers. */
void ai_work_add_dependency(ai_work_node *node, u64 dependency_id);
void ai_work_add_input(ai_work_node *node, ai_tensor *tensor);
void ai_work_add_output(ai_work_node *node, ai_tensor *tensor);

bool ai_work_dependencies_done(const ai_work_graph *graph, const ai_work_node *node);
void ai_work_refresh_states(ai_work_graph *graph);

/*
 * Complete one work node and consume each of its input edges exactly once.
 * The final consumer event is connected to the Step 9 reclamation manager,
 * which reclaims immediately when possible and queues retryable deferrals.
 */
ai_work_status ai_work_complete(ai_work_node *node);

bool ai_work_has_deadline(const ai_work_node *node);
bool ai_work_is_profiled(const ai_work_node *node);
u64 ai_work_estimated_total_bytes(const ai_work_node *node);
u64 ai_work_logical_total_bytes(const ai_work_node *node);

nx_handle_t ai_tensor_producer_work(const ai_tensor *tensor);
u32 ai_tensor_consumer_count(const ai_tensor *tensor);
u32 ai_tensor_consumers_remaining(const ai_tensor *tensor);
bool ai_tensor_has_consumer(const ai_tensor *tensor, nx_handle_t work_handle);
bool ai_tensor_consumer_done(const ai_tensor *tensor, nx_handle_t work_handle);

const char *ai_work_state_name(ai_work_state state);
const char *ai_op_name(ai_op op);
const char *ai_work_class_name(ai_work_class work_class);
const char *ai_work_qos_name(ai_work_qos qos);
const char *ai_work_status_name(ai_work_status status);

#endif
