#include <ai/device.h>
#include <kernel/panic.h>

static ai_device devices[AI_MAX_DEVICES];
static ai_device *device_registry[AI_MAX_DEVICES];
static u32 device_count_value;
static ai_device_link links[AI_MAX_DEVICE_LINKS];
static ai_device_stats stats;

static bool valid_kind(ai_device_mask kind) {
    const u32 value = (u32)kind;
    if (value == 0 || (value & ~((u32)AI_DEVICE_VALID_MASK)) != 0) {
        return false;
    }

    /* A concrete device has exactly one class bit in Step 8. */
    return (value & (value - 1u)) == 0;
}

static bool checked_add_u64(u64 a, u64 b, u64 *out) {
    if (out == NULL || a > (~(u64)0) - b) {
        return false;
    }
    *out = a + b;
    return true;
}

/* Conservative ceil(bytes * 1e9 / bandwidth) without 128-bit helpers. */
static bool bytes_to_ns(u64 bytes, u64 bandwidth, u64 *out_ns) {
    if (out_ns == NULL || bandwidth == 0) {
        return false;
    }
    if (bytes == 0) {
        *out_ns = 0;
        return true;
    }

    const u64 billion = 1000000000ull;

    if (bandwidth >= billion) {
        /* Floor bytes/ns so the result is conservative (never too optimistic). */
        const u64 bytes_per_ns = bandwidth / billion;
        if (bytes_per_ns == 0) {
            return false;
        }
        *out_ns = bytes / bytes_per_ns + ((bytes % bytes_per_ns) != 0 ? 1ull : 0ull);
        if (*out_ns == 0) {
            *out_ns = 1;
        }
        return true;
    }

    /* Here rem < bandwidth < 1e9, so rem * 1e9 cannot overflow u64. */
    const u64 whole = bytes / bandwidth;
    const u64 rem = bytes % bandwidth;
    if (whole > (~(u64)0) / billion) {
        return false;
    }
    u64 ns = whole * billion;
    const u64 product = rem * billion;
    const u64 fractional = product / bandwidth + ((product % bandwidth) != 0 ? 1ull : 0ull);
    if (!checked_add_u64(ns, fractional, &ns)) {
        return false;
    }
    if (ns == 0) {
        ns = 1;
    }
    *out_ns = ns;
    return true;
}

static ai_device_link *find_link(ai_device_id_t from, ai_device_id_t to) {
    for (u32 i = 0; i < AI_MAX_DEVICE_LINKS; ++i) {
        if (links[i].valid && links[i].from == from && links[i].to == to) {
            return &links[i];
        }
    }
    return NULL;
}

static ai_device_status set_one_link(
    ai_device_id_t from,
    ai_device_id_t to,
    u64 bandwidth_bytes_per_sec,
    u64 latency_ns
) {
    ai_device_link *existing = find_link(from, to);
    if (existing != NULL) {
        existing->bandwidth_bytes_per_sec = bandwidth_bytes_per_sec;
        existing->latency_ns = latency_ns;
        return AI_DEVICE_OK;
    }

    for (u32 i = 0; i < AI_MAX_DEVICE_LINKS; ++i) {
        if (!links[i].valid) {
            links[i].from = from;
            links[i].to = to;
            links[i].bandwidth_bytes_per_sec = bandwidth_bytes_per_sec;
            links[i].latency_ns = latency_ns;
            links[i].valid = true;
            ++stats.links_configured;
            return AI_DEVICE_OK;
        }
    }
    return AI_DEVICE_ERR_REGISTRY_FULL;
}

ai_device_status ai_device_try_register(
    const ai_device_desc *desc,
    ai_device **out_device
) {
    if (out_device != NULL) {
        *out_device = NULL;
    }
    if (desc == NULL || out_device == NULL || desc->name == NULL) {
        return AI_DEVICE_ERR_INVALID_ARGUMENT;
    }
    if (!valid_kind(desc->kind)) {
        return AI_DEVICE_ERR_INVALID_KIND;
    }
    if (device_count_value >= AI_MAX_DEVICES) {
        return AI_DEVICE_ERR_REGISTRY_FULL;
    }

    ai_device *device = &devices[device_count_value];
    if (!nx_object_register_ex(
            &device->object,
            NX_OBJECT_DEVICE,
            desc->name,
            NX_OBJECT_FLAG_KERNEL | NX_OBJECT_FLAG_PERSISTENT,
            NX_OWNER_KERNEL,
            NULL)) {
        return AI_DEVICE_ERR_OBJECT_REGISTRY_FULL;
    }

    device->kind = desc->kind;
    device->memory_bytes = desc->memory_bytes;
    device->numa_node = desc->numa_node;
    device->compute_ops_per_sec = desc->compute_ops_per_sec;
    device->memory_bandwidth_bytes_per_sec = desc->memory_bandwidth_bytes_per_sec;

    device_registry[device_count_value] = device;
    ++device_count_value;
    ++stats.devices_registered;
    *out_device = device;
    return AI_DEVICE_OK;
}

ai_device *ai_device_register(const ai_device_desc *desc) {
    ai_device *device = NULL;
    const ai_device_status status = ai_device_try_register(desc, &device);
    if (status != AI_DEVICE_OK) {
        panic(ai_device_status_name(status));
    }
    return device;
}

ai_device *ai_device_lookup(ai_device_id_t id) {
    if (id == AI_DEVICE_INVALID) {
        return NULL;
    }
    nx_object *object = nx_object_lookup((nx_handle_t)id, NX_OBJECT_DEVICE);
    return object != NULL ? (ai_device *)object : NULL;
}

ai_device *ai_device_default(ai_device_mask kind) {
    for (u32 i = 0; i < device_count_value; ++i) {
        ai_device *device = device_registry[i];
        if (device != NULL && device->object.state == NX_OBJECT_LIVE && device->kind == kind) {
            return device;
        }
    }
    return NULL;
}

ai_device *ai_device_at(u32 index) {
    return index < device_count_value ? device_registry[index] : NULL;
}

u32 ai_device_count(void) {
    return device_count_value;
}

const ai_device_stats *ai_device_get_stats(void) {
    return &stats;
}

bool ai_device_supports_mask(const ai_device *device, u32 allowed_mask) {
    return device != NULL && (((u32)device->kind & allowed_mask) != 0);
}

ai_device_status ai_device_set_link(
    ai_device_id_t from,
    ai_device_id_t to,
    u64 bandwidth_bytes_per_sec,
    u64 latency_ns,
    bool bidirectional
) {
    if (from == AI_DEVICE_INVALID || to == AI_DEVICE_INVALID || bandwidth_bytes_per_sec == 0) {
        return AI_DEVICE_ERR_INVALID_ARGUMENT;
    }
    if (ai_device_lookup(from) == NULL || ai_device_lookup(to) == NULL) {
        return AI_DEVICE_ERR_NOT_FOUND;
    }

    ai_device_status status = set_one_link(from, to, bandwidth_bytes_per_sec, latency_ns);
    if (status != AI_DEVICE_OK) {
        return status;
    }
    if (bidirectional && from != to) {
        status = set_one_link(to, from, bandwidth_bytes_per_sec, latency_ns);
    }
    return status;
}

ai_device_status ai_device_estimate_transfer_ns(
    ai_device_id_t from,
    ai_device_id_t to,
    u64 bytes,
    u64 *out_ns
) {
    if (out_ns != NULL) {
        *out_ns = 0;
    }
    if (out_ns == NULL || from == AI_DEVICE_INVALID || to == AI_DEVICE_INVALID) {
        ++stats.transfer_estimate_failures;
        return AI_DEVICE_ERR_INVALID_ARGUMENT;
    }
    if (ai_device_lookup(from) == NULL || ai_device_lookup(to) == NULL) {
        ++stats.transfer_estimate_failures;
        return AI_DEVICE_ERR_NOT_FOUND;
    }
    if (from == to || bytes == 0) {
        ++stats.transfer_estimates;
        *out_ns = 0;
        return AI_DEVICE_OK;
    }

    ai_device_link *link = find_link(from, to);
    if (link == NULL) {
        ++stats.transfer_estimate_failures;
        return AI_DEVICE_ERR_LINK_NOT_FOUND;
    }

    u64 payload_ns = 0;
    if (!bytes_to_ns(bytes, link->bandwidth_bytes_per_sec, &payload_ns) ||
        !checked_add_u64(link->latency_ns, payload_ns, out_ns)) {
        ++stats.transfer_estimate_failures;
        return AI_DEVICE_ERR_OVERFLOW;
    }

    ++stats.transfer_estimates;
    return AI_DEVICE_OK;
}

void ai_device_system_init(void) {
    device_count_value = 0;
    stats.devices_registered = 0;
    stats.links_configured = 0;
    stats.transfer_estimates = 0;
    stats.transfer_estimate_failures = 0;

    for (u32 i = 0; i < AI_MAX_DEVICES; ++i) {
        device_registry[i] = NULL;
    }
    for (u32 i = 0; i < AI_MAX_DEVICE_LINKS; ++i) {
        links[i].from = AI_DEVICE_INVALID;
        links[i].to = AI_DEVICE_INVALID;
        links[i].bandwidth_bytes_per_sec = 0;
        links[i].latency_ns = 0;
        links[i].valid = false;
    }

    /*
     * Synthetic reference topology used until hardware discovery/drivers exist.
     * Values are intentionally documented as model parameters, not claims about
     * the machine that booted Nexora.
     */
    const ai_device_desc cpu_desc = {
        .name = "cpu0",
        .kind = AI_DEVICE_CPU,
        .memory_bytes = 32ull * 1024ull * 1024ull * 1024ull,
        .numa_node = 0,
        .compute_ops_per_sec = 500ull * 1000ull * 1000ull * 1000ull,
        .memory_bandwidth_bytes_per_sec = 100ull * 1000ull * 1000ull * 1000ull
    };
    const ai_device_desc gpu_desc = {
        .name = "gpu0",
        .kind = AI_DEVICE_GPU,
        .memory_bytes = 16ull * 1024ull * 1024ull * 1024ull,
        .numa_node = 0,
        .compute_ops_per_sec = 20ull * 1000ull * 1000ull * 1000ull * 1000ull,
        .memory_bandwidth_bytes_per_sec = 800ull * 1000ull * 1000ull * 1000ull
    };
    const ai_device_desc npu_desc = {
        .name = "npu0",
        .kind = AI_DEVICE_NPU,
        .memory_bytes = 8ull * 1024ull * 1024ull * 1024ull,
        .numa_node = 0,
        .compute_ops_per_sec = 10ull * 1000ull * 1000ull * 1000ull * 1000ull,
        .memory_bandwidth_bytes_per_sec = 400ull * 1000ull * 1000ull * 1000ull
    };

    ai_device *cpu = ai_device_register(&cpu_desc);
    ai_device *gpu = ai_device_register(&gpu_desc);
    ai_device *npu = ai_device_register(&npu_desc);

    if (ai_device_set_link(cpu->object.handle, gpu->object.handle,
                           32ull * 1000ull * 1000ull * 1000ull, 2000ull, true) != AI_DEVICE_OK ||
        ai_device_set_link(cpu->object.handle, npu->object.handle,
                           24ull * 1000ull * 1000ull * 1000ull, 3000ull, true) != AI_DEVICE_OK ||
        ai_device_set_link(gpu->object.handle, npu->object.handle,
                           12ull * 1000ull * 1000ull * 1000ull, 5000ull, true) != AI_DEVICE_OK) {
        panic("device link initialization failed");
    }
}

const char *ai_device_kind_name(ai_device_mask kind) {
    switch (kind) {
        case AI_DEVICE_CPU: return "CPU";
        case AI_DEVICE_GPU: return "GPU";
        case AI_DEVICE_NPU: return "NPU";
        case AI_DEVICE_NIC: return "NIC";
        case AI_DEVICE_STORAGE: return "STORAGE";
        default: return "UNKNOWN";
    }
}

const char *ai_device_status_name(ai_device_status status) {
    switch (status) {
        case AI_DEVICE_OK: return "device ok";
        case AI_DEVICE_ERR_INVALID_ARGUMENT: return "device invalid argument";
        case AI_DEVICE_ERR_INVALID_KIND: return "device invalid kind";
        case AI_DEVICE_ERR_REGISTRY_FULL: return "device registry full";
        case AI_DEVICE_ERR_OBJECT_REGISTRY_FULL: return "object registry full";
        case AI_DEVICE_ERR_NOT_FOUND: return "device not found";
        case AI_DEVICE_ERR_LINK_NOT_FOUND: return "device link not found";
        case AI_DEVICE_ERR_OVERFLOW: return "device estimate overflow";
        default: return "device unknown error";
    }
}
