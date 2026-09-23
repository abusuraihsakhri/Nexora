#include <ai/inference.h>

static ai_model models[AI_INFER_MAX_MODELS];
static ai_kv_state kv_states[AI_INFER_MAX_KV_STATES];
static ai_infer_request requests[AI_INFER_MAX_REQUESTS];
static ai_inference_budget budget;
static u64 next_model_id = 1;
static u64 next_kv_id = 1;
static u64 next_request_id = 1;
static u64 next_batch_id = 1;

static bool str_equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

static bool add_u64_overflow(u64 a, u64 b, u64 *out) {
    if (!out || a > (~0ull - b)) return true;
    *out = a + b;
    return false;
}

static u64 budget_limit(void) {
    if (budget.capacity_bytes <= budget.reserved_bytes) return 0;
    u64 usable = budget.capacity_bytes - budget.reserved_bytes;
    u32 pct = budget.high_watermark_percent;
    if (pct == 0 || pct > 100) pct = 100;
    return (usable / 100ull) * (u64)pct + ((usable % 100ull) * (u64)pct) / 100ull;
}

static bool budget_can_commit(u64 bytes) {
    u64 next = 0;
    if (add_u64_overflow(budget.committed_bytes, bytes, &next)) return false;
    return next <= budget_limit();
}

static bool budget_commit(u64 bytes) {
    if (!budget_can_commit(bytes)) return false;
    budget.committed_bytes += bytes;
    return true;
}

static void budget_release(u64 bytes) {
    if (bytes >= budget.committed_bytes) {
        budget.committed_bytes = 0;
    } else {
        budget.committed_bytes -= bytes;
    }
}

void ai_inference_init(u64 capacity_bytes, u64 reserved_bytes, u32 high_watermark_percent) {
    for (u32 i = 0; i < AI_INFER_MAX_MODELS; ++i) models[i].id = 0;
    for (u32 i = 0; i < AI_INFER_MAX_KV_STATES; ++i) kv_states[i].id = 0;
    for (u32 i = 0; i < AI_INFER_MAX_REQUESTS; ++i) requests[i].id = 0;

    budget.capacity_bytes = capacity_bytes;
    budget.reserved_bytes = reserved_bytes > capacity_bytes ? capacity_bytes : reserved_bytes;
    budget.committed_bytes = 0;
    budget.high_watermark_percent = high_watermark_percent > 100 ? 100 : high_watermark_percent;

    next_model_id = 1;
    next_kv_id = 1;
    next_request_id = 1;
    next_batch_id = 1;
}

ai_model *ai_model_register(const char *name, u64 weights_bytes, u32 preferred_device_mask, bool persistent) {
    if (!name || weights_bytes == 0 || preferred_device_mask == 0) return NULL;

    for (u32 i = 0; i < AI_INFER_MAX_MODELS; ++i) {
        if (models[i].id != 0 && str_equal(models[i].name, name)) return NULL;
    }

    for (u32 i = 0; i < AI_INFER_MAX_MODELS; ++i) {
        if (models[i].id != 0) continue;
        ai_model *model = &models[i];
        model->id = next_model_id++;
        model->name = name;
        model->weights_bytes = weights_bytes;
        model->preferred_device_mask = preferred_device_mask;
        model->location = AI_LOC_CPU_RAM;
        model->state = AI_MODEL_UNLOADED;
        model->persistent = persistent;
        return model;
    }
    return NULL;
}

ai_model *ai_model_lookup(u64 model_id) {
    if (model_id == 0) return NULL;
    for (u32 i = 0; i < AI_INFER_MAX_MODELS; ++i) {
        if (models[i].id == model_id) return &models[i];
    }
    return NULL;
}

bool ai_model_set_resident(u64 model_id, ai_tensor_location location) {
    ai_model *model = ai_model_lookup(model_id);
    if (!model) return false;
    if (model->state == AI_MODEL_RESIDENT) {
        model->location = location;
        return true;
    }
    if (!budget_commit(model->weights_bytes)) return false;
    model->location = location;
    model->state = AI_MODEL_RESIDENT;
    return true;
}

bool ai_model_set_unloaded(u64 model_id) {
    ai_model *model = ai_model_lookup(model_id);
    if (!model || model->persistent) return false;
    if (model->state == AI_MODEL_RESIDENT) budget_release(model->weights_bytes);
    model->state = AI_MODEL_UNLOADED;
    model->location = AI_LOC_CPU_RAM;
    return true;
}

ai_kv_state *ai_kv_lookup(u64 model_id, u64 session_id) {
    for (u32 i = 0; i < AI_INFER_MAX_KV_STATES; ++i) {
        if (kv_states[i].id != 0 && kv_states[i].model_id == model_id && kv_states[i].session_id == session_id) {
            return &kv_states[i];
        }
    }
    return NULL;
}

ai_kv_state *ai_kv_register(u64 model_id, u64 session_id, u64 bytes, ai_tensor_location location, bool pinned, u64 now_ns) {
    if (!ai_model_lookup(model_id) || session_id == 0 || bytes == 0) return NULL;
    ai_kv_state *existing = ai_kv_lookup(model_id, session_id);
    if (existing) return existing;
    if (!budget_commit(bytes)) return NULL;

    for (u32 i = 0; i < AI_INFER_MAX_KV_STATES; ++i) {
        if (kv_states[i].id != 0) continue;
        ai_kv_state *kv = &kv_states[i];
        kv->id = next_kv_id++;
        kv->model_id = model_id;
        kv->session_id = session_id;
        kv->bytes = bytes;
        kv->location = location;
        kv->pinned = pinned;
        kv->last_touch_ns = now_ns;
        return kv;
    }

    budget_release(bytes);
    return NULL;
}

bool ai_kv_touch(u64 model_id, u64 session_id, u64 now_ns) {
    ai_kv_state *kv = ai_kv_lookup(model_id, session_id);
    if (!kv) return false;
    kv->last_touch_ns = now_ns;
    return true;
}

ai_infer_request *ai_infer_submit(
    u64 model_id,
    u64 session_id,
    u32 input_tokens,
    u32 max_output_tokens,
    u64 enqueue_ns,
    u64 deadline_ns,
    u64 estimated_memory_bytes
) {
    ai_admission_result result = AI_ADMIT_OK;
    ai_model *model = ai_model_lookup(model_id);
    if (model_id == 0 || session_id == 0 || input_tokens == 0 || max_output_tokens == 0 || deadline_ns < enqueue_ns) {
        result = AI_ADMIT_INVALID;
    } else if (!model) {
        result = AI_ADMIT_UNKNOWN_MODEL;
    } else if (model->state != AI_MODEL_RESIDENT) {
        result = AI_ADMIT_MODEL_NOT_RESIDENT;
    } else if (!budget_can_commit(estimated_memory_bytes)) {
        result = AI_ADMIT_MEMORY_PRESSURE;
    }

    ai_infer_request *slot = NULL;
    for (u32 i = 0; i < AI_INFER_MAX_REQUESTS; ++i) {
        if (requests[i].id == 0 || requests[i].state == AI_REQUEST_DONE || requests[i].state == AI_REQUEST_REJECTED) {
            slot = &requests[i];
            break;
        }
    }
    if (!slot) return NULL;

    slot->id = next_request_id++;
    slot->model_id = model_id;
    slot->session_id = session_id;
    slot->input_tokens = input_tokens;
    slot->max_output_tokens = max_output_tokens;
    slot->enqueue_ns = enqueue_ns;
    slot->deadline_ns = deadline_ns;
    slot->estimated_memory_bytes = estimated_memory_bytes;
    slot->admission = result;
    slot->state = (result == AI_ADMIT_OK) ? AI_REQUEST_QUEUED : AI_REQUEST_REJECTED;

    if (result == AI_ADMIT_OK) {
        if (!budget_commit(estimated_memory_bytes)) {
            slot->admission = AI_ADMIT_MEMORY_PRESSURE;
            slot->state = AI_REQUEST_REJECTED;
        }
    }
    return slot;
}

static ai_infer_request *request_lookup(u64 request_id) {
    for (u32 i = 0; i < AI_INFER_MAX_REQUESTS; ++i) {
        if (requests[i].id == request_id) return &requests[i];
    }
    return NULL;
}

bool ai_infer_form_batch(
    u64 model_id,
    u32 max_requests,
    u32 max_input_tokens,
    u64 now_ns,
    ai_infer_batch *out_batch
) {
    if (!out_batch || !ai_model_lookup(model_id) || max_requests == 0 || max_input_tokens == 0) return false;
    if (max_requests > AI_INFER_MAX_BATCH_REQUESTS) max_requests = AI_INFER_MAX_BATCH_REQUESTS;

    out_batch->id = next_batch_id++;
    out_batch->model_id = model_id;
    out_batch->request_count = 0;
    out_batch->total_input_tokens = 0;
    out_batch->estimated_memory_bytes = 0;
    out_batch->earliest_deadline_ns = ~0ull;

    for (;;) {
        ai_infer_request *best = NULL;
        for (u32 i = 0; i < AI_INFER_MAX_REQUESTS; ++i) {
            ai_infer_request *req = &requests[i];
            if (req->id == 0 || req->state != AI_REQUEST_QUEUED || req->model_id != model_id) continue;
            if (req->deadline_ns < now_ns) {
                budget_release(req->estimated_memory_bytes);
                req->state = AI_REQUEST_REJECTED;
                req->admission = AI_ADMIT_INVALID;
                continue;
            }
            if (out_batch->request_count >= max_requests) break;
            if (req->input_tokens > max_input_tokens - out_batch->total_input_tokens) continue;
            if (!best || req->deadline_ns < best->deadline_ns ||
                (req->deadline_ns == best->deadline_ns && req->enqueue_ns < best->enqueue_ns)) {
                best = req;
            }
        }
        if (!best || out_batch->request_count >= max_requests) break;
        out_batch->request_ids[out_batch->request_count++] = best->id;
        out_batch->total_input_tokens += best->input_tokens;
        u64 batch_memory = 0;
        if (add_u64_overflow(out_batch->estimated_memory_bytes, best->estimated_memory_bytes, &batch_memory)) {
            break;
        }
        out_batch->estimated_memory_bytes = batch_memory;
        if (best->deadline_ns < out_batch->earliest_deadline_ns) out_batch->earliest_deadline_ns = best->deadline_ns;
        best->state = AI_REQUEST_BATCHED;
    }

    if (out_batch->request_count == 0) {
        out_batch->earliest_deadline_ns = 0;
        return false;
    }
    return true;
}

bool ai_infer_mark_running(const ai_infer_batch *batch) {
    if (!batch || batch->request_count == 0 || batch->request_count > AI_INFER_MAX_BATCH_REQUESTS) return false;
    for (u32 i = 0; i < batch->request_count; ++i) {
        ai_infer_request *req = request_lookup(batch->request_ids[i]);
        if (!req || req->state != AI_REQUEST_BATCHED) return false;
    }
    for (u32 i = 0; i < batch->request_count; ++i) request_lookup(batch->request_ids[i])->state = AI_REQUEST_RUNNING;
    return true;
}

bool ai_infer_complete_request(u64 request_id) {
    ai_infer_request *req = request_lookup(request_id);
    if (!req || (req->state != AI_REQUEST_RUNNING && req->state != AI_REQUEST_BATCHED && req->state != AI_REQUEST_QUEUED)) return false;
    budget_release(req->estimated_memory_bytes);
    req->state = AI_REQUEST_DONE;
    return true;
}

bool ai_infer_plan_prefetch(
    u64 tensor_id,
    ai_tensor_location source,
    ai_tensor_location destination,
    u64 bytes,
    u64 need_by_ns,
    ai_prefetch_plan *out_plan
) {
    if (!out_plan || tensor_id == 0 || bytes == 0 || source == destination) return false;
    out_plan->tensor_id = tensor_id;
    out_plan->source = source;
    out_plan->destination = destination;
    out_plan->bytes = bytes;
    out_plan->need_by_ns = need_by_ns;
    out_plan->admitted = budget_can_commit(bytes);
    return true;
}

const ai_inference_budget *ai_inference_get_budget(void) {
    return &budget;
}

u64 ai_inference_headroom_bytes(void) {
    u64 limit = budget_limit();
    return budget.committed_bytes >= limit ? 0 : limit - budget.committed_bytes;
}

u32 ai_inference_pending_requests(void) {
    u32 count = 0;
    for (u32 i = 0; i < AI_INFER_MAX_REQUESTS; ++i) {
        ai_request_state state = requests[i].state;
        if (requests[i].id != 0 && (state == AI_REQUEST_QUEUED || state == AI_REQUEST_BATCHED || state == AI_REQUEST_RUNNING)) ++count;
    }
    return count;
}

const char *ai_model_state_name(ai_model_state state) {
    switch (state) {
        case AI_MODEL_UNLOADED: return "UNLOADED";
        case AI_MODEL_LOADING: return "LOADING";
        case AI_MODEL_RESIDENT: return "RESIDENT";
        case AI_MODEL_EVICTING: return "EVICTING";
        case AI_MODEL_FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

const char *ai_request_state_name(ai_request_state state) {
    switch (state) {
        case AI_REQUEST_EMPTY: return "EMPTY";
        case AI_REQUEST_QUEUED: return "QUEUED";
        case AI_REQUEST_BATCHED: return "BATCHED";
        case AI_REQUEST_RUNNING: return "RUNNING";
        case AI_REQUEST_DONE: return "DONE";
        case AI_REQUEST_REJECTED: return "REJECTED";
        default: return "UNKNOWN";
    }
}

const char *ai_admission_result_name(ai_admission_result result) {
    switch (result) {
        case AI_ADMIT_OK: return "OK";
        case AI_ADMIT_INVALID: return "INVALID";
        case AI_ADMIT_UNKNOWN_MODEL: return "UNKNOWN_MODEL";
        case AI_ADMIT_MODEL_NOT_RESIDENT: return "MODEL_NOT_RESIDENT";
        case AI_ADMIT_MEMORY_PRESSURE: return "MEMORY_PRESSURE";
        case AI_ADMIT_REQUEST_TABLE_FULL: return "REQUEST_TABLE_FULL";
        default: return "UNKNOWN";
    }
}
