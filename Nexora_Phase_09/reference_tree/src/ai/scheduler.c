#include <ai/scheduler.h>

void ai_scheduler_init(ai_scheduler *scheduler, ai_work_graph *graph) {
    scheduler->graph = graph;
    scheduler->dispatch_count = 0;
    ai_work_refresh_states(graph);
}

ai_work_node *ai_scheduler_pick(ai_scheduler *scheduler) {
    ai_work_refresh_states(scheduler->graph);

    ai_work_node *best = NULL;

    for (u32 i = 0; i < scheduler->graph->node_count; ++i) {
        ai_work_node *candidate = scheduler->graph->nodes[i];

        if (candidate->state != AI_WORK_READY) {
            continue;
        }

        if (!best ||
            candidate->priority > best->priority ||
            (candidate->priority == best->priority &&
             candidate->deadline_ns < best->deadline_ns)) {
            best = candidate;
        }
    }

    return best;
}

void ai_scheduler_mark_running(ai_scheduler *scheduler, ai_work_node *node) {
    if (!node) return;
    node->state = AI_WORK_RUNNING;
    scheduler->dispatch_count++;
}

void ai_scheduler_mark_done(ai_scheduler *scheduler, ai_work_node *node) {
    (void)scheduler;
    if (!node) return;
    node->state = AI_WORK_DONE;
}
