#include <assert.h>
#include <stdio.h>
#include <ai/inference.h>

#define MIB (1024ull * 1024ull)

int main(void) {
    ai_inference_init(1024ull * MIB, 128ull * MIB, 90);
    const ai_inference_budget *budget = ai_inference_get_budget();
    assert(budget->capacity_bytes == 1024ull * MIB);
    assert(ai_inference_headroom_bytes() > 0);

    ai_model *model = ai_model_register("llm-a", 256ull * MIB, AI_DEVICE_GPU, true);
    assert(model != NULL);
    assert(ai_model_register("llm-a", 128ull * MIB, AI_DEVICE_GPU, true) == NULL);
    assert(ai_model_set_resident(model->id, AI_LOC_GPU_HBM));
    assert(model->state == AI_MODEL_RESIDENT);
    assert(!ai_model_set_unloaded(model->id));

    ai_kv_state *kv = ai_kv_register(model->id, 77, 64ull * MIB, AI_LOC_GPU_HBM, true, 100);
    assert(kv != NULL && kv->pinned);
    assert(ai_kv_touch(model->id, 77, 120));
    assert(ai_kv_lookup(model->id, 77)->last_touch_ns == 120);

    ai_infer_request *r1 = ai_infer_submit(model->id, 1, 100, 50, 1000, 4000, 16ull * MIB);
    ai_infer_request *r2 = ai_infer_submit(model->id, 2, 120, 50, 1001, 2500, 16ull * MIB);
    ai_infer_request *r3 = ai_infer_submit(model->id, 3, 80, 50, 1002, 3000, 16ull * MIB);
    assert(r1 && r2 && r3);
    assert(r1->admission == AI_ADMIT_OK && r2->admission == AI_ADMIT_OK && r3->admission == AI_ADMIT_OK);
    assert(ai_inference_pending_requests() == 3);

    ai_infer_batch batch;
    assert(ai_infer_form_batch(model->id, 2, 250, 1500, &batch));
    assert(batch.request_count == 2);
    assert(batch.request_ids[0] == r2->id);
    assert(batch.request_ids[1] == r3->id);
    assert(batch.earliest_deadline_ns == 2500);
    assert(ai_infer_mark_running(&batch));
    assert(ai_infer_complete_request(r2->id));
    assert(ai_infer_complete_request(r3->id));
    assert(ai_inference_pending_requests() == 1);

    ai_infer_request *expired = ai_infer_submit(model->id, 4, 10, 10, 100, 110, 8ull * MIB);
    assert(expired && expired->state == AI_REQUEST_QUEUED);
    ai_infer_batch ignored;
    (void)ai_infer_form_batch(model->id, 4, 1000, 200, &ignored);
    assert(expired->state == AI_REQUEST_REJECTED);

    ai_infer_request *huge = ai_infer_submit(model->id, 5, 10, 10, 1000, 2000, 900ull * MIB);
    assert(huge && huge->state == AI_REQUEST_REJECTED);
    assert(huge->admission == AI_ADMIT_MEMORY_PRESSURE);

    ai_prefetch_plan plan;
    assert(ai_infer_plan_prefetch(44, AI_LOC_CPU_RAM, AI_LOC_GPU_HBM, 8ull * MIB, 5000, &plan));
    assert(plan.admitted);
    assert(!ai_infer_plan_prefetch(44, AI_LOC_GPU_HBM, AI_LOC_GPU_HBM, 8ull * MIB, 5000, &plan));

    puts("Phase 8 inference tests: PASS");
    return 0;
}
