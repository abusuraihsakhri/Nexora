/* Example only: adapt names to the actual Phase 15 scheduler/device path. */
#include <ai/telemetry.h>
#include <ai/fault_injection.h>

extern ai_telemetry_buffer g_ai_telem;
extern ai_fault_injector g_ai_faults;

int phase16_before_device_submit(u64 now_ns, u32 cpu_id, u64 work_id) {
    if (ai_fault_should_fail(&g_ai_faults, AI_FAULT_DEVICE_SUBMIT)) {
        ai_telemetry_emit(&g_ai_telem, now_ns, AI_TELEM_FAULT_INJECT,
                          cpu_id, work_id, AI_FAULT_DEVICE_SUBMIT, 0);
        return -1;
    }
    ai_telemetry_emit(&g_ai_telem, now_ns, AI_TELEM_WORK_DISPATCH,
                      cpu_id, work_id, 0, 0);
    return 0;
}
