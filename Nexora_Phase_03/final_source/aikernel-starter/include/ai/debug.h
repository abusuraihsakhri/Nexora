#ifndef NEXORA_AI_DEBUG_H
#define NEXORA_AI_DEBUG_H

#include <kernel/types.h>
#include <ai/work.h>

void ai_debug_dump_tensor(const struct ai_tensor *tensor);
void ai_debug_dump_tensors(void);
void ai_debug_dump_graph(const ai_work_graph *graph);
void ai_debug_dump_trace(u32 max_events);
void ai_debug_dump_memory_summary(void);

#endif
