#ifndef AIKERNEL_AI_SCHEDULER_H
#define AIKERNEL_AI_SCHEDULER_H

#include <ai/work.h>
#include <ai/device.h>

typedef enum {
    AI_PLACEMENT_OK = 0,
    AI_PLACEMENT_ERR_INVALID_ARGUMENT,
    AI_PLACEMENT_ERR_DEVICE_NOT_ALLOWED,
    AI_PLACEMENT_ERR_DEVICE_NOT_FOUND,
    AI_PLACEMENT_ERR_NO_ROUTE,
    AI_PLACEMENT_ERR_NO_FEASIBLE_DEVICE,
    AI_PLACEMENT_ERR_OVERFLOW
} ai_placement_status;

typedef struct {
    ai_work_node *work;
    ai_device_id_t device_id;

    u64 input_transfer_bytes;
    u64 output_transfer_bytes;
    u64 transfer_ns;
    u64 execution_ns;
    u64 total_ns;

    u32 input_transfers;
    u32 output_transfers;
    bool preferred_device;
} ai_dispatch_choice;

typedef struct {
    ai_work_graph *graph;
    u64 dispatch_count;
    u64 placement_evaluations;
    u64 placement_failures;
} ai_scheduler;

void ai_scheduler_init(ai_scheduler *scheduler, ai_work_graph *graph);

ai_placement_status ai_placement_evaluate(
    const ai_work_node *node,
    ai_device_id_t device_id,
    ai_dispatch_choice *out_choice
);

ai_placement_status ai_placement_choose(
    const ai_work_node *node,
    ai_dispatch_choice *out_choice
);

ai_placement_status ai_scheduler_pick_dispatch(
    ai_scheduler *scheduler,
    ai_dispatch_choice *out_choice
);

/* Backward-compatible work-only picker. */
ai_work_node *ai_scheduler_pick(ai_scheduler *scheduler);

ai_work_status ai_scheduler_mark_running_on(
    ai_scheduler *scheduler,
    ai_work_node *node,
    ai_device_id_t device_id
);

void ai_scheduler_mark_running(ai_scheduler *scheduler, ai_work_node *node);
ai_work_status ai_scheduler_complete(ai_scheduler *scheduler, ai_work_node *node);
void ai_scheduler_mark_done(ai_scheduler *scheduler, ai_work_node *node);

const char *ai_placement_status_name(ai_placement_status status);

#endif
