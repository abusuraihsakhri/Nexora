#ifndef AIKERNEL_AI_SCHEDULER_H
#define AIKERNEL_AI_SCHEDULER_H

#include <ai/work.h>

typedef struct {
    ai_work_graph *graph;
    u64 dispatch_count;
} ai_scheduler;

void ai_scheduler_init(ai_scheduler *scheduler, ai_work_graph *graph);
ai_work_node *ai_scheduler_pick(ai_scheduler *scheduler);
void ai_scheduler_mark_running(ai_scheduler *scheduler, ai_work_node *node);
void ai_scheduler_mark_done(ai_scheduler *scheduler, ai_work_node *node);

#endif
