#include <ai/scheduler.h>
#include <ai/reclaim.h>
#include <ai/instrument.h>

static bool checked_add_u64(u64 a, u64 b, u64 *out) {
    if (out == NULL || a > (~(u64)0) - b) {
        return false;
    }
    *out = a + b;
    return true;
}

/* Conservative ceil(amount * 1e9 / rate), avoiding compiler 128-bit helpers. */
static bool rate_time_ns(u64 amount, u64 rate_per_sec, u64 *out_ns) {
    if (out_ns == NULL || rate_per_sec == 0) {
        return false;
    }
    if (amount == 0) {
        *out_ns = 0;
        return true;
    }

    const u64 billion = 1000000000ull;
    if (rate_per_sec >= billion) {
        const u64 per_ns = rate_per_sec / billion;
        if (per_ns == 0) {
            return false;
        }
        *out_ns = amount / per_ns + ((amount % per_ns) != 0 ? 1ull : 0ull);
        if (*out_ns == 0) {
            *out_ns = 1;
        }
        return true;
    }

    const u64 whole = amount / rate_per_sec;
    const u64 rem = amount % rate_per_sec;
    if (whole > (~(u64)0) / billion) {
        return false;
    }

    u64 ns = whole * billion;
    const u64 product = rem * billion; /* rem < rate < 1e9 => safe. */
    const u64 fractional = product / rate_per_sec +
        ((product % rate_per_sec) != 0 ? 1ull : 0ull);
    if (!checked_add_u64(ns, fractional, &ns)) {
        return false;
    }
    if (ns == 0) {
        ns = 1;
    }
    *out_ns = ns;
    return true;
}

static u32 qos_rank(ai_work_qos qos) {
    switch (qos) {
        case AI_WORK_QOS_LATENCY: return 3;
        case AI_WORK_QOS_DEFAULT: return 2;
        case AI_WORK_QOS_THROUGHPUT: return 1;
        case AI_WORK_QOS_BACKGROUND: return 0;
        case AI_WORK_QOS_COUNT: break;
        default: break;
    }
    return 0;
}

static bool work_precedes(
    const ai_work_node *candidate,
    const ai_dispatch_choice *candidate_choice,
    const ai_work_node *best,
    const ai_dispatch_choice *best_choice
) {
    if (best == NULL) {
        return true;
    }
    if (candidate->priority != best->priority) {
        return candidate->priority > best->priority;
    }

    const bool candidate_deadline = ai_work_has_deadline(candidate);
    const bool best_deadline = ai_work_has_deadline(best);
    if (candidate_deadline != best_deadline) {
        return candidate_deadline;
    }
    if (candidate_deadline && candidate->deadline_ns != best->deadline_ns) {
        return candidate->deadline_ns < best->deadline_ns;
    }

    const u32 candidate_qos = qos_rank(candidate->qos);
    const u32 best_qos = qos_rank(best->qos);
    if (candidate_qos != best_qos) {
        return candidate_qos > best_qos;
    }

    if (candidate->estimated_duration_ns != 0 && best->estimated_duration_ns != 0 &&
        candidate->estimated_duration_ns != best->estimated_duration_ns) {
        return candidate->estimated_duration_ns < best->estimated_duration_ns;
    }

    /* Step 8: locality cost is a tie-break after submitter scheduling intent. */
    if (candidate_choice != NULL && best_choice != NULL &&
        candidate_choice->total_ns != best_choice->total_ns) {
        return candidate_choice->total_ns < best_choice->total_ns;
    }

    return candidate->object.id < best->object.id;
}

static ai_placement_status add_transfer(
    ai_device_id_t from,
    ai_device_id_t to,
    u64 bytes,
    u64 *total_bytes,
    u64 *total_ns,
    u32 *transfer_count
) {
    if (from == AI_DEVICE_INVALID || to == AI_DEVICE_INVALID || from == to || bytes == 0) {
        return AI_PLACEMENT_OK;
    }

    u64 ns = 0;
    const ai_device_status status = ai_device_estimate_transfer_ns(from, to, bytes, &ns);
    if (status == AI_DEVICE_ERR_LINK_NOT_FOUND) {
        return AI_PLACEMENT_ERR_NO_ROUTE;
    }
    if (status != AI_DEVICE_OK) {
        return status == AI_DEVICE_ERR_OVERFLOW
            ? AI_PLACEMENT_ERR_OVERFLOW
            : AI_PLACEMENT_ERR_DEVICE_NOT_FOUND;
    }

    u64 new_bytes = 0;
    u64 new_ns = 0;
    if (!checked_add_u64(*total_bytes, bytes, &new_bytes) ||
        !checked_add_u64(*total_ns, ns, &new_ns)) {
        return AI_PLACEMENT_ERR_OVERFLOW;
    }

    *total_bytes = new_bytes;
    *total_ns = new_ns;
    ++(*transfer_count);
    return AI_PLACEMENT_OK;
}

static ai_placement_status estimate_execution_ns(
    const ai_work_node *node,
    const ai_device *device,
    u64 *out_ns
) {
    u64 compute_ns = 0;
    u64 memory_ns = 0;
    bool have_compute = false;
    bool have_memory = false;

    if (node->estimated_ops != 0 && device->compute_ops_per_sec != 0) {
        if (!rate_time_ns(node->estimated_ops, device->compute_ops_per_sec, &compute_ns)) {
            return AI_PLACEMENT_ERR_OVERFLOW;
        }
        have_compute = true;
    }

    u64 traffic = 0;
    if (!checked_add_u64(node->estimated_read_bytes, node->estimated_write_bytes, &traffic) ||
        !checked_add_u64(traffic, node->estimated_scratch_bytes, &traffic)) {
        return AI_PLACEMENT_ERR_OVERFLOW;
    }
    if (traffic != 0 && device->memory_bandwidth_bytes_per_sec != 0) {
        if (!rate_time_ns(traffic, device->memory_bandwidth_bytes_per_sec, &memory_ns)) {
            return AI_PLACEMENT_ERR_OVERFLOW;
        }
        have_memory = true;
    }

    if (have_compute || have_memory) {
        *out_ns = compute_ns > memory_ns ? compute_ns : memory_ns;
        return AI_PLACEMENT_OK;
    }

    *out_ns = node->estimated_duration_ns;
    return AI_PLACEMENT_OK;
}

ai_placement_status ai_placement_evaluate(
    const ai_work_node *node,
    ai_device_id_t device_id,
    ai_dispatch_choice *out_choice
) {
    if (node == NULL || out_choice == NULL || device_id == AI_DEVICE_INVALID) {
        return AI_PLACEMENT_ERR_INVALID_ARGUMENT;
    }

    ai_device *device = ai_device_lookup(device_id);
    if (device == NULL) {
        return AI_PLACEMENT_ERR_DEVICE_NOT_FOUND;
    }
    if (!ai_device_supports_mask(device, node->device_mask)) {
        return AI_PLACEMENT_ERR_DEVICE_NOT_ALLOWED;
    }

    ai_dispatch_choice choice;
    choice.work = (ai_work_node *)node;
    choice.device_id = device_id;
    choice.input_transfer_bytes = 0;
    choice.output_transfer_bytes = 0;
    choice.transfer_ns = 0;
    choice.execution_ns = 0;
    choice.total_ns = 0;
    choice.input_transfers = 0;
    choice.output_transfers = 0;
    choice.preferred_device = node->preferred_device != AI_DEVICE_INVALID &&
                              node->preferred_device == device_id;

    for (u32 i = 0; i < node->input_count; ++i) {
        const ai_tensor *tensor = node->inputs[i];
        if (tensor == NULL) {
            continue;
        }
        ai_device_id_t source = tensor->resident_device;
        if (source == AI_DEVICE_INVALID) {
            source = tensor->preferred_device;
        }
        ai_placement_status status = add_transfer(
            source,
            device_id,
            tensor->logical_bytes,
            &choice.input_transfer_bytes,
            &choice.transfer_ns,
            &choice.input_transfers
        );
        if (status != AI_PLACEMENT_OK) {
            return status;
        }
    }

    for (u32 i = 0; i < node->output_count; ++i) {
        const ai_tensor *tensor = node->outputs[i];
        if (tensor == NULL) {
            continue;
        }
        ai_device_id_t target = tensor->preferred_device;
        if (target == AI_DEVICE_INVALID) {
            target = tensor->resident_device;
        }
        ai_placement_status status = add_transfer(
            device_id,
            target,
            tensor->logical_bytes,
            &choice.output_transfer_bytes,
            &choice.transfer_ns,
            &choice.output_transfers
        );
        if (status != AI_PLACEMENT_OK) {
            return status;
        }
    }

    ai_placement_status status = estimate_execution_ns(node, device, &choice.execution_ns);
    if (status != AI_PLACEMENT_OK) {
        return status;
    }
    if (!checked_add_u64(choice.transfer_ns, choice.execution_ns, &choice.total_ns)) {
        return AI_PLACEMENT_ERR_OVERFLOW;
    }

    *out_choice = choice;
    return AI_PLACEMENT_OK;
}

ai_placement_status ai_placement_choose(
    const ai_work_node *node,
    ai_dispatch_choice *out_choice
) {
    if (node == NULL || out_choice == NULL) {
        return AI_PLACEMENT_ERR_INVALID_ARGUMENT;
    }

    bool found = false;
    ai_dispatch_choice best;

    for (u32 i = 0; i < ai_device_count(); ++i) {
        ai_device *device = ai_device_at(i);
        if (device == NULL || !ai_device_supports_mask(device, node->device_mask)) {
            continue;
        }

        ai_dispatch_choice candidate;
        const ai_placement_status status = ai_placement_evaluate(
            node, device->object.handle, &candidate
        );
        if (status != AI_PLACEMENT_OK) {
            continue;
        }

        if (!found || candidate.total_ns < best.total_ns ||
            (candidate.total_ns == best.total_ns && candidate.preferred_device &&
             !best.preferred_device) ||
            (candidate.total_ns == best.total_ns &&
             candidate.preferred_device == best.preferred_device &&
             candidate.device_id < best.device_id)) {
            best = candidate;
            found = true;
        }
    }

    if (!found) {
        return AI_PLACEMENT_ERR_NO_FEASIBLE_DEVICE;
    }
    *out_choice = best;
    return AI_PLACEMENT_OK;
}

void ai_scheduler_init(ai_scheduler *scheduler, ai_work_graph *graph) {
    if (scheduler == NULL) {
        return;
    }
    scheduler->graph = graph;
    scheduler->dispatch_count = 0;
    scheduler->placement_evaluations = 0;
    scheduler->placement_failures = 0;
    ai_work_refresh_states(graph);
}

ai_placement_status ai_scheduler_pick_dispatch(
    ai_scheduler *scheduler,
    ai_dispatch_choice *out_choice
) {
    if (scheduler == NULL || scheduler->graph == NULL || out_choice == NULL) {
        return AI_PLACEMENT_ERR_INVALID_ARGUMENT;
    }

    (void)ai_reclaim_retry_deferred();
    ai_work_refresh_states(scheduler->graph);

    ai_work_node *best_node = NULL;
    ai_dispatch_choice best_choice;
    bool found = false;

    for (u32 i = 0; i < scheduler->graph->node_count; ++i) {
        ai_work_node *candidate = scheduler->graph->nodes[i];
        if (candidate == NULL || candidate->state != AI_WORK_READY) {
            continue;
        }

        ai_dispatch_choice choice;
        ++scheduler->placement_evaluations;
        const ai_placement_status status = ai_placement_choose(candidate, &choice);
        if (status != AI_PLACEMENT_OK) {
            ++scheduler->placement_failures;
            continue;
        }

        if (!found || work_precedes(candidate, &choice, best_node, &best_choice)) {
            best_node = candidate;
            best_choice = choice;
            found = true;
        }
    }

    if (!found) {
        return AI_PLACEMENT_ERR_NO_FEASIBLE_DEVICE;
    }
    *out_choice = best_choice;
    return AI_PLACEMENT_OK;
}

ai_work_node *ai_scheduler_pick(ai_scheduler *scheduler) {
    if (scheduler == NULL || scheduler->graph == NULL) {
        return NULL;
    }

    /* Preserve Step 7 behavior for host/unit contexts without a device model. */
    if (ai_device_count() == 0) {
        (void)ai_reclaim_retry_deferred();
        ai_work_refresh_states(scheduler->graph);
        ai_work_node *best = NULL;
        for (u32 i = 0; i < scheduler->graph->node_count; ++i) {
            ai_work_node *candidate = scheduler->graph->nodes[i];
            if (candidate == NULL || candidate->state != AI_WORK_READY) {
                continue;
            }
            if (work_precedes(candidate, NULL, best, NULL)) {
                best = candidate;
            }
        }
        return best;
    }

    ai_dispatch_choice choice;
    return ai_scheduler_pick_dispatch(scheduler, &choice) == AI_PLACEMENT_OK
        ? choice.work
        : NULL;
}

ai_work_status ai_scheduler_mark_running_on(
    ai_scheduler *scheduler,
    ai_work_node *node,
    ai_device_id_t device_id
) {
    if (scheduler == NULL || node == NULL || node->state != AI_WORK_READY) {
        return AI_WORK_ERR_INVALID_STATE;
    }

    ai_dispatch_choice choice;
    if (ai_placement_evaluate(node, device_id, &choice) != AI_PLACEMENT_OK) {
        return AI_WORK_ERR_INVALID_PREFERRED_DEVICE;
    }

    node->selected_device = device_id;
    node->state = AI_WORK_RUNNING;
    ++scheduler->dispatch_count;
    ai_device *trace_device = ai_device_lookup(device_id);
    ai_trace_record(
        AI_TRACE_WORK_DISPATCH,
        node->object.id,
        trace_device != NULL ? trace_device->object.id : 0,
        choice.total_ns
    );
    return AI_WORK_OK;
}

void ai_scheduler_mark_running(ai_scheduler *scheduler, ai_work_node *node) {
    if (scheduler == NULL || node == NULL) {
        return;
    }

    if (ai_device_count() == 0) {
        node->state = AI_WORK_RUNNING;
        ++scheduler->dispatch_count;
        ai_trace_record(AI_TRACE_WORK_DISPATCH, node->object.id, 0, 0);
        return;
    }

    ai_dispatch_choice choice;
    if (ai_placement_choose(node, &choice) == AI_PLACEMENT_OK) {
        (void)ai_scheduler_mark_running_on(scheduler, node, choice.device_id);
    }
}

static void commit_output_residency(ai_work_node *node) {
    if (node == NULL || node->selected_device == AI_DEVICE_INVALID) {
        return;
    }

    for (u32 i = 0; i < node->output_count; ++i) {
        ai_tensor *tensor = node->outputs[i];
        if (tensor == NULL || !ai_tensor_is_backed(tensor)) {
            continue;
        }

        ai_device_id_t final_device = tensor->preferred_device;
        if (final_device == AI_DEVICE_INVALID) {
            final_device = node->selected_device;
        }
        (void)ai_tensor_set_resident_device(tensor, final_device);
    }
}

void ai_scheduler_mark_done(ai_scheduler *scheduler, ai_work_node *node) {
    (void)ai_scheduler_complete(scheduler, node);
}

ai_work_status ai_scheduler_complete(ai_scheduler *scheduler, ai_work_node *node) {
    if (scheduler == NULL || node == NULL) {
        return AI_WORK_ERR_INVALID_ARGUMENT;
    }

    commit_output_residency(node);
    return ai_work_complete(node);
}

const char *ai_placement_status_name(ai_placement_status status) {
    switch (status) {
        case AI_PLACEMENT_OK: return "placement ok";
        case AI_PLACEMENT_ERR_INVALID_ARGUMENT: return "placement invalid argument";
        case AI_PLACEMENT_ERR_DEVICE_NOT_ALLOWED: return "placement device not allowed";
        case AI_PLACEMENT_ERR_DEVICE_NOT_FOUND: return "placement device not found";
        case AI_PLACEMENT_ERR_NO_ROUTE: return "placement no transfer route";
        case AI_PLACEMENT_ERR_NO_FEASIBLE_DEVICE: return "placement no feasible device";
        case AI_PLACEMENT_ERR_OVERFLOW: return "placement cost overflow";
        default: return "placement unknown error";
    }
}
