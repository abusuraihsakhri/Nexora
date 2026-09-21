#include "nexora/phase10/audit.h"

void nx_audit_init(nx_audit_log_t *log)
{
    if (!log) return;
    nx_p10_memzero(log, sizeof(*log));
    log->next_seq = 1;
}

void nx_audit_write(nx_audit_log_t *log,
                    nx_p10_time_t time,
                    nx_audit_event_type_t type,
                    nx_p10_id_t subject_id,
                    nx_p10_id_t actor_id,
                    int32_t code,
                    uint64_t data0,
                    uint64_t data1)
{
    nx_audit_event_t *e;
    uint64_t seq;
    if (!log) return;
    seq = log->next_seq++;
    e = &log->entries[(seq - 1) % NX_P10_AUDIT_CAPACITY];
    e->seq = seq;
    e->time = time;
    e->type = type;
    e->subject_id = subject_id;
    e->actor_id = actor_id;
    e->code = code;
    e->data0 = data0;
    e->data1 = data1;
}

size_t nx_audit_snapshot(const nx_audit_log_t *log,
                         nx_audit_event_t *out,
                         size_t out_capacity)
{
    uint64_t count, start_seq, seq;
    size_t written = 0;
    if (!log || !out || out_capacity == 0) return 0;
    count = log->next_seq > 1 ? log->next_seq - 1 : 0;
    if (count > NX_P10_AUDIT_CAPACITY) count = NX_P10_AUDIT_CAPACITY;
    if (count > out_capacity) count = out_capacity;
    start_seq = (log->next_seq - 1) >= count ? (log->next_seq - count) : 1;
    for (seq = start_seq; seq < log->next_seq && written < count; ++seq) {
        const nx_audit_event_t *e = &log->entries[(seq - 1) % NX_P10_AUDIT_CAPACITY];
        if (e->seq == seq) out[written++] = *e;
    }
    return written;
}
