#ifndef NEXORA_AI_INFERENCE_H
#define NEXORA_AI_INFERENCE_H

#include <kernel/types.h>
#include <ai/tensor.h>
#include <ai/device.h>

#define AI_INFER_MAX_MODELS 16
#define AI_INFER_MAX_KV_STATES 64
#define AI_INFER_MAX_REQUESTS 128
#define AI_INFER_MAX_BATCH_REQUESTS 16
#define AI_INFER_MAX_PREFETCH_PLANS 64

typedef enum {
    AI_MODEL_UNLOADED = 0,
    AI_MODEL_LOADING,
    AI_MODEL_RESIDENT,
    AI_MODEL_EVICTING,
    AI_MODEL_FAILED
} ai_model_state;

typedef enum {
    AI_REQUEST_EMPTY = 0,
    AI_REQUEST_QUEUED,
    AI_REQUEST_BATCHED,
    AI_REQUEST_RUNNING,
    AI_REQUEST_DONE,
    AI_REQUEST_REJECTED
} ai_request_state;

typedef enum {
    AI_ADMIT_OK = 0,
    AI_ADMIT_INVALID,
    AI_ADMIT_UNKNOWN_MODEL,
    AI_ADMIT_MODEL_NOT_RESIDENT,
    AI_ADMIT_MEMORY_PRESSURE,
    AI_ADMIT_REQUEST_TABLE_FULL
} ai_admission_result;

typedef struct {
    u64 id;
    const char *name;
    u64 weights_bytes;
    u32 preferred_device_mask;
    ai_tensor_location location;
    ai_model_state state;
    bool persistent;
} ai_model;

typedef struct {
    u64 id;
    u64 model_id;
    u64 session_id;
    u64 bytes;
    ai_tensor_location location;
    bool pinned;
    u64 last_touch_ns;
} ai_kv_state;

typedef struct {
    u64 id;
    u64 model_id;
    u64 session_id;
    u32 input_tokens;
    u32 max_output_tokens;
    u64 enqueue_ns;
    u64 deadline_ns;
    u64 estimated_memory_bytes;
    ai_request_state state;
    ai_admission_result admission;
} ai_infer_request;

typedef struct {
    u64 id;
    u64 model_id;
    u64 request_ids[AI_INFER_MAX_BATCH_REQUESTS];
    u32 request_count;
    u32 total_input_tokens;
    u64 estimated_memory_bytes;
    u64 earliest_deadline_ns;
} ai_infer_batch;

typedef struct {
    u64 tensor_id;
    ai_tensor_location source;
    ai_tensor_location destination;
    u64 bytes;
    u64 need_by_ns;
    bool admitted;
} ai_prefetch_plan;

typedef struct {
    u64 capacity_bytes;
    u64 reserved_bytes;
    u64 committed_bytes;
    u32 high_watermark_percent;
} ai_inference_budget;

void ai_inference_init(u64 capacity_bytes, u64 reserved_bytes, u32 high_watermark_percent);

ai_model *ai_model_register(const char *name, u64 weights_bytes, u32 preferred_device_mask, bool persistent);
ai_model *ai_model_lookup(u64 model_id);
bool ai_model_set_resident(u64 model_id, ai_tensor_location location);
bool ai_model_set_unloaded(u64 model_id);

ai_kv_state *ai_kv_register(u64 model_id, u64 session_id, u64 bytes, ai_tensor_location location, bool pinned, u64 now_ns);
ai_kv_state *ai_kv_lookup(u64 model_id, u64 session_id);
bool ai_kv_touch(u64 model_id, u64 session_id, u64 now_ns);

ai_infer_request *ai_infer_submit(
    u64 model_id,
    u64 session_id,
    u32 input_tokens,
    u32 max_output_tokens,
    u64 enqueue_ns,
    u64 deadline_ns,
    u64 estimated_memory_bytes
);

bool ai_infer_form_batch(
    u64 model_id,
    u32 max_requests,
    u32 max_input_tokens,
    u64 now_ns,
    ai_infer_batch *out_batch
);

bool ai_infer_mark_running(const ai_infer_batch *batch);
bool ai_infer_complete_request(u64 request_id);

bool ai_infer_plan_prefetch(
    u64 tensor_id,
    ai_tensor_location source,
    ai_tensor_location destination,
    u64 bytes,
    u64 need_by_ns,
    ai_prefetch_plan *out_plan
);

const ai_inference_budget *ai_inference_get_budget(void);
u64 ai_inference_headroom_bytes(void);
u32 ai_inference_pending_requests(void);
const char *ai_model_state_name(ai_model_state state);
const char *ai_request_state_name(ai_request_state state);
const char *ai_admission_result_name(ai_admission_result result);

#endif
