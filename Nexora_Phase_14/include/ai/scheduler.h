#ifndef AIKERNEL_AI_SCHEDULER_H
#define AIKERNEL_AI_SCHEDULER_H

#include <ai/work.h>

typedef struct {
    ai_work_graph *graph;
    u64 dispatch_count;
    u64 pick_count;
    u64 candidate_scan_count;
    u64 deadlock_count;
} ai_scheduler;

typedef struct {
    bool completed;
    bool deadlocked;
    u32 completed_nodes;
    u32 failed_nodes;
    u64 dispatches;
    u64 picks;
    u64 candidates_scanned;
} ai_scheduler_run_report;

void ai_scheduler_init(ai_scheduler *scheduler, ai_work_graph *graph);
ai_work_node *ai_scheduler_pick(ai_scheduler *scheduler);
void ai_scheduler_mark_running(ai_scheduler *scheduler, ai_work_node *node);
void ai_scheduler_mark_done(ai_scheduler *scheduler, ai_work_node *node);
void ai_scheduler_mark_failed(ai_scheduler *scheduler, ai_work_node *node);
bool ai_scheduler_run_to_completion(ai_scheduler *scheduler, ai_scheduler_run_report *report);

#endif
