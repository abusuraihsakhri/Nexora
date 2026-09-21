#include <ai/debug.h>
#include <ai/instrument.h>
#include <ai/tensor.h>
#include <ai/device.h>
#include <ai/lifetime.h>
#include <ai/reclaim.h>
#include <kernel/memory_object.h>
#include <kernel/printk.h>

static void print_id_or_none(u64 id) {
    if (id == 0) {
        kputs("none");
    } else {
        kprint_u64(id);
    }
}

void ai_debug_dump_tensor(const ai_tensor *tensor) {
    if (tensor == NULL) {
        kputs("tensor <null>\n");
        return;
    }

    kputs("tensor id=");
    kprint_u64(tensor->object.id);
    kputs(" name=");
    kputs(tensor->object.name);
    kputs(" class=");
    kputs(ai_tensor_class_name(tensor->tensor_class));
    kputs(" lifetime=");
    kputs(ai_tensor_lifetime_name(tensor->lifetime));
    kputs(" residency=");
    kputs(ai_tensor_residency_name(tensor->residency));
    kputs(" bytes=");
    kprint_u64(tensor->logical_bytes);
    kputs(" consumers=");
    kprint_u64(tensor->consumers_remaining);
    kputs("/");
    kprint_u64(tensor->consumer_count);
    kputs(" producer=");

    nx_object *producer = nx_object_lookup(tensor->producer_work, NX_OBJECT_WORK);
    print_id_or_none(producer != NULL ? producer->id : 0);

    kputs(" backing=");
    nx_memory *memory = ai_tensor_backing(tensor);
    print_id_or_none(memory != NULL ? memory->object.id : 0);
    kputs("\n");
}

void ai_debug_dump_tensors(void) {
    kputs("\n[Nexora tensor table]\n");
    for (u64 i = 0; i < ai_tensor_count(); ++i) {
        ai_tensor *tensor = ai_tensor_at(i);
        if (tensor != NULL && tensor->object.state == NX_OBJECT_LIVE) {
            ai_debug_dump_tensor(tensor);
        }
    }
}

void ai_debug_dump_graph(const ai_work_graph *graph) {
    kputs("\n[Nexora work graph]\n");
    if (graph == NULL) {
        kputs("<null graph>\n");
        return;
    }

    for (u32 i = 0; i < graph->node_count; ++i) {
        const ai_work_node *node = graph->nodes[i];
        if (node == NULL) {
            continue;
        }

        kputs("work id=");
        kprint_u64(node->object.id);
        kputs(" name=");
        kputs(node->object.name);
        kputs(" op=");
        kputs(ai_op_name(node->op));
        kputs(" state=");
        kputs(ai_work_state_name(node->state));
        kputs(" selected_device=");
        ai_device *device = ai_device_lookup(node->selected_device);
        kputs(device != NULL ? device->object.name : "none");
        kputs(" inputs=[");
        for (u32 j = 0; j < node->input_count; ++j) {
            if (j != 0) kputs(",");
            kprint_u64(node->inputs[j] != NULL ? node->inputs[j]->object.id : 0);
        }
        kputs("] outputs=[");
        for (u32 j = 0; j < node->output_count; ++j) {
            if (j != 0) kputs(",");
            kprint_u64(node->outputs[j] != NULL ? node->outputs[j]->object.id : 0);
        }
        kputs("]\n");
    }

    const ai_work_graph_stats *graph_stats = ai_work_graph_get_stats(graph);
    if (graph_stats != NULL) {
        kputs("graph stats: nodes=");
        kprint_u64(graph_stats->nodes_created);
        kputs(" producer_links=");
        kprint_u64(graph_stats->producer_links);
        kputs(" consumer_links=");
        kprint_u64(graph_stats->consumer_links);
        kputs(" final_consumer_events=");
        kprint_u64(graph_stats->final_consumer_events);
        kputs(" auto_reclaimed=");
        kprint_u64(graph_stats->automatic_reclaim_successes);
        kputs(" deferred=");
        kprint_u64(graph_stats->automatic_reclaim_deferred);
        kputs("\n");
    }
}

void ai_debug_dump_trace(u32 max_events) {
    kputs("\n[Nexora trace]\n");
    u32 count = ai_trace_count();
    u32 start = 0;
    if (max_events != 0 && count > max_events) {
        start = count - max_events;
    }

    for (u32 i = start; i < count; ++i) {
        const ai_trace_event *event = ai_trace_at(i);
        if (event == NULL) {
            continue;
        }
        kputs("#");
        kprint_u64(event->sequence);
        kputs(" ");
        kputs(ai_trace_type_name(event->type));
        kputs(" object=");
        kprint_u64(event->object_id);
        kputs(" related=");
        kprint_u64(event->related_id);
        kputs(" value=");
        kprint_u64(event->value);
        kputs(" resident=");
        kprint_u64(event->resident_bytes);
        kputs("\n");
    }

    const ai_instrument_stats *trace_stats = ai_instrument_get_stats();
    kputs("trace stats: recorded=");
    kprint_u64(trace_stats->events_recorded);
    kputs(" dropped=");
    kprint_u64(trace_stats->events_dropped);
    kputs(" sampled_peak_resident=");
    kprint_u64(trace_stats->sampled_peak_resident_bytes);
    kputs("\n");
}

void ai_debug_dump_memory_summary(void) {
    const nx_memory_stats *memory = nx_memory_get_stats();
    const ai_tensor_stats *tensors = ai_tensor_get_stats();
    const ai_lifetime_stats *lifetime = ai_lifetime_get_stats();
    const ai_reclaim_stats *reclaim = ai_reclaim_get_stats();

    kputs("\n[Nexora memory summary]\n");
    kputs("resident=");
    kprint_u64(memory->resident_bytes);
    kputs(" peak_resident=");
    kprint_u64(memory->peak_resident_bytes);
    kputs(" requested=");
    kprint_u64(memory->requested_bytes);
    kputs("\n");

    kputs("tensor_logical=");
    kprint_u64(tensors->logical_bytes);
    kputs(" peak_tensor_logical=");
    kprint_u64(tensors->peak_logical_bytes);
    kputs(" live_tensors=");
    kprint_u64(tensors->live);
    kputs("\n");

    kputs("lifetime_reclaim_successes=");
    kprint_u64(lifetime->reclaim_successes);
    kputs(" resident_reclaimed=");
    kprint_u64(lifetime->resident_bytes_reclaimed);
    kputs(" final_consumer_successes=");
    kprint_u64(reclaim->final_consumer_successes);
    kputs(" pressure_reclaimed=");
    kprint_u64(reclaim->pressure_resident_bytes_reclaimed);
    kputs("\n");
}
