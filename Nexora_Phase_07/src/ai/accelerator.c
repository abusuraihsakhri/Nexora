#include <ai/accelerator.h>
#include <kernel/printk.h>

static ai_accel_device *registry[AI_ACCEL_MAX_DEVICES];
static u32 count = 0;

void ai_accel_system_init(void) {
    count = 0;
    for (u32 i = 0; i < AI_ACCEL_MAX_DEVICES; ++i) {
        registry[i] = NULL;
    }
}

bool ai_accel_register(ai_accel_device *device) {
    if (!device || !device->ops || count >= AI_ACCEL_MAX_DEVICES) {
        return false;
    }
    device->id = count + 1;
    registry[count++] = device;
    return true;
}

u32 ai_accel_count(void) {
    return count;
}

ai_accel_device *ai_accel_at(u32 index) {
    if (index >= count) {
        return NULL;
    }
    return registry[index];
}

ai_accel_device *ai_accel_find_for_mask(u32 device_mask) {
    for (u32 i = 0; i < count; ++i) {
        ai_accel_device *dev = registry[i];
        if (dev->online && (dev->device_mask & device_mask) != 0u) {
            return dev;
        }
    }
    return NULL;
}

bool ai_accel_submit(ai_accel_device *device, ai_accel_command *command) {
    if (!device || !device->online || !device->ops || !device->ops->submit || !command) {
        return false;
    }

    command->status = AI_ACCEL_STATUS_QUEUED;
    if (!device->ops->submit(device, command)) {
        command->status = AI_ACCEL_STATUS_FAILED;
        device->failed++;
        return false;
    }

    device->submitted++;
    if (device->ops->kick) {
        device->ops->kick(device);
    }
    return true;
}

void ai_accel_poll_all(void) {
    for (u32 i = 0; i < count; ++i) {
        ai_accel_device *dev = registry[i];
        if (dev->online && dev->ops && dev->ops->poll) {
            dev->ops->poll(dev);
        }
    }
}

bool ai_accel_wait(ai_accel_device *device, ai_accel_command *command, u32 spin_limit) {
    if (!device || !command) {
        return false;
    }

    for (u32 spin = 0; spin < spin_limit; ++spin) {
        if (device->ops && device->ops->poll) {
            device->ops->poll(device);
        }
        if (command->status == AI_ACCEL_STATUS_OK ||
            command->status == AI_ACCEL_STATUS_SIMULATED) {
            return true;
        }
        if (command->status == AI_ACCEL_STATUS_FAILED ||
            command->status == AI_ACCEL_STATUS_UNSUPPORTED) {
            return false;
        }
    }
    return false;
}

bool ai_accel_submit_work(ai_accel_device *device, const ai_work_node *node, ai_accel_command *command) {
    if (!device || !node || !command) {
        return false;
    }

    command->command_id = device->submitted + 1;
    command->type = AI_ACCEL_CMD_WORK;
    command->status = AI_ACCEL_STATUS_EMPTY;
    command->work_id = node->id;
    command->op = node->op;
    command->src_phys = 0;
    command->dst_phys = 0;
    command->bytes = 0;
    command->flags = 0;
    return ai_accel_submit(device, command);
}

const char *ai_accel_status_name(ai_accel_status status) {
    switch (status) {
        case AI_ACCEL_STATUS_EMPTY: return "EMPTY";
        case AI_ACCEL_STATUS_QUEUED: return "QUEUED";
        case AI_ACCEL_STATUS_RUNNING: return "RUNNING";
        case AI_ACCEL_STATUS_OK: return "OK";
        case AI_ACCEL_STATUS_SIMULATED: return "SIMULATED";
        case AI_ACCEL_STATUS_UNSUPPORTED: return "UNSUPPORTED";
        case AI_ACCEL_STATUS_FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

const char *ai_accel_transport_name(ai_accel_transport transport) {
    switch (transport) {
        case AI_ACCEL_TRANSPORT_SIM: return "sim";
        case AI_ACCEL_TRANSPORT_PCI: return "pci";
        case AI_ACCEL_TRANSPORT_MMIO: return "mmio";
        default: return "unknown";
    }
}

void ai_accel_print_summary(void) {
    kputs("Accelerator backends: ");
    kprint_u64(count);
    kputs("\n");
    for (u32 i = 0; i < count; ++i) {
        const ai_accel_device *dev = registry[i];
        kputs("  accel id=");
        kprint_u64(dev->id);
        kputs(" name=");
        kputs(dev->name);
        kputs(" transport=");
        kputs(ai_accel_transport_name(dev->transport));
        kputs(" online=");
        kputs(dev->online ? "yes" : "no");
        kputs(" submitted=");
        kprint_u64(dev->submitted);
        kputs(" completed=");
        kprint_u64(dev->completed);
        kputs(" failed=");
        kprint_u64(dev->failed);
        kputs(" bytes=");
        kprint_u64(dev->bytes_moved);
        kputs("\n");
    }
}
