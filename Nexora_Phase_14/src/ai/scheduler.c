#include <ai/scheduler.h>

void ai_scheduler_init(ai_scheduler *scheduler, ai_work_graph *graph) {
    if (!scheduler) return;
    scheduler->graph = graph;
    scheduler->dispatch_count = 0;
    scheduler->pick_count = 0;
    scheduler->candidate_scan_count = 0;
    scheduler->deadlock_count = 0;
    if (graph) ai_work_refresh_states(graph);
}

ai_work_node *ai_scheduler_pick(ai_scheduler *scheduler) {
    if (!scheduler || !scheduler->graph) return NULL;
    ai_work_refresh_states(scheduler->graph);
    scheduler->pick_count++;

    ai_work_node *best = NULL;

    for (u32 i = 0; i < scheduler->graph->node_count; ++i) {
        ai_work_node *candidate = scheduler->graph->nodes[i];
        scheduler->candidate_scan_count++;

        if (!candidate || candidate->state != AI_WORK_READY) {
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
    if (!scheduler || !node) return;
    node->state = AI_WORK_RUNNING;
    scheduler->dispatch_count++;
}

void ai_scheduler_mark_done(ai_scheduler *scheduler, ai_work_node *node) {
    (void)scheduler;
    if (!node) return;
    node->state = AI_WORK_DONE;
}

void ai_scheduler_mark_failed(ai_scheduler *scheduler, ai_work_node *node) {
    (void)scheduler;
    if (!node) return;
    node->state = AI_WORK_FAILED;
}

bool ai_scheduler_run_to_completion(ai_scheduler *scheduler, ai_scheduler_run_report *report) {
    ai_scheduler_run_report local;
    ai_scheduler_run_report *r = report ? report : &local;
    r->completed = false;
    r->deadlocked = false;
    r->completed_nodes = 0;
    r->failed_nodes = 0;
    r->dispatches = 0;
    r->picks = 0;
    r->candidates_scanned = 0;

    if (!scheduler || !scheduler->graph) return false;

    u32 max_iterations = scheduler->graph->node_count + 1;
    for (u32 iteration = 0; iteration < max_iterations; ++iteration) {
        u32 done = ai_work_graph_done_count(scheduler->graph);
        u32 failed = 0;
        for (u32 i = 0; i < scheduler->graph->node_count; ++i) {
            const ai_work_node *node = scheduler->graph->nodes[i];
            if (node && node->state == AI_WORK_FAILED) failed++;
        }

        if (done + failed == scheduler->graph->node_count) {
            r->completed = failed == 0;
            r->completed_nodes = done;
            r->failed_nodes = failed;
            r->dispatches = scheduler->dispatch_count;
            r->picks = scheduler->pick_count;
            r->candidates_scanned = scheduler->candidate_scan_count;
            return r->completed;
        }

        ai_async_queue queue;
        ai_async_queue_init(&queue);

        ai_work_node *node = ai_scheduler_pick(scheduler);
        if (!node) {
            scheduler->deadlock_count++;
            r->deadlocked = true;
            r->completed_nodes = done;
            r->failed_nodes = failed;
            r->dispatches = scheduler->dispatch_count;
            r->picks = scheduler->pick_count;
            r->candidates_scanned = scheduler->candidate_scan_count;
            return false;
        }

        ai_scheduler_mark_running(scheduler, node);
        ai_async_queue_enqueue(&queue, node);
        ai_async_queue_drain(&queue, scheduler);
    }

    scheduler->deadlock_count++;
    r->deadlocked = true;
    r->completed_nodes = ai_work_graph_done_count(scheduler->graph);
    r->dispatches = scheduler->dispatch_count;
    r->picks = scheduler->pick_count;
    r->candidates_scanned = scheduler->candidate_scan_count;
    return false;
}

void ai_async_queue_init(ai_async_queue *q) {
    if (!q) return;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->enqueued_count = 0;
    q->drained_count = 0;
    for (u32 i = 0; i < AI_ASYNC_QUEUE_CAPACITY; ++i) {
        q->in_flight[i] = NULL;
    }
}

bool ai_async_queue_enqueue(ai_async_queue *q, ai_work_node *node) {
    if (!q || !node || q->count >= AI_ASYNC_QUEUE_CAPACITY) return false;
    q->in_flight[q->tail] = node;
    q->tail = (q->tail + 1) % AI_ASYNC_QUEUE_CAPACITY;
    q->count++;
    q->enqueued_count++;
    return true;
}

ai_work_node *ai_async_queue_dequeue(ai_async_queue *q) {
    if (!q || q->count == 0) return NULL;
    ai_work_node *node = q->in_flight[q->head];
    q->in_flight[q->head] = NULL;
    q->head = (q->head + 1) % AI_ASYNC_QUEUE_CAPACITY;
    q->count--;
    return node;
}

bool ai_async_queue_is_empty(const ai_async_queue *q) {
    return !q || q->count == 0;
}

u32 ai_async_queue_drain(ai_async_queue *q, ai_scheduler *scheduler) {
    if (!q) return 0;
    u32 drained = 0;
    while (q->count > 0) {
        ai_work_node *node = ai_async_queue_dequeue(q);
        if (node) {
            ai_scheduler_mark_done(scheduler, node);
            drained++;
            q->drained_count++;
        }
    }
    return drained;
}
