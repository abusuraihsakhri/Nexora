#include <drivers/nex_accel_sim.h>
#include <ai/accelerator.h>
#include <kernel/memory.h>
#include <arch/x86_64/io.h>

#define NEX_SIM_QUEUE_DEPTH 32

typedef struct {
    ai_accel_command *slots[NEX_SIM_QUEUE_DEPTH];
    u32 producer;
    u32 consumer;
    u32 pending;
} nex_sim_state;

static ai_accel_device sim_device;
static nex_sim_state sim_state;

static void copy_phys(u64 src_phys, u64 dst_phys, u64 bytes) {
    const volatile u8 *src = (const volatile u8 *)(usize)src_phys;
    volatile u8 *dst = (volatile u8 *)(usize)dst_phys;
    for (u64 i = 0; i < bytes; ++i) {
        dst[i] = src[i];
    }
    io_fence();
}

static bool sim_submit(ai_accel_device *device, ai_accel_command *command) {
    nex_sim_state *state = (nex_sim_state *)device->driver_data;
    if (!state || state->pending >= NEX_SIM_QUEUE_DEPTH) {
        return false;
    }

    state->slots[state->producer] = command;
    state->producer = (state->producer + 1u) % NEX_SIM_QUEUE_DEPTH;
    state->pending++;
    return true;
}

static void sim_execute(ai_accel_device *device, ai_accel_command *command) {
    command->status = AI_ACCEL_STATUS_RUNNING;

    switch (command->type) {
        case AI_ACCEL_CMD_NOP:
            command->status = AI_ACCEL_STATUS_OK;
            break;
        case AI_ACCEL_CMD_COPY:
            if (command->src_phys == 0 || command->dst_phys == 0) {
                command->status = AI_ACCEL_STATUS_FAILED;
                device->failed++;
                break;
            }
            copy_phys(command->src_phys, command->dst_phys, command->bytes);
            device->bytes_moved += command->bytes;
            command->status = AI_ACCEL_STATUS_OK;
            break;
        case AI_ACCEL_CMD_WORK:
            /*
             * This backend intentionally models queueing/dispatch/completion,
             * not real numerical execution. Returning SIMULATED prevents the
             * kernel from confusing a transport-path test with real inference.
             */
            command->status = AI_ACCEL_STATUS_SIMULATED;
            break;
        default:
            command->status = AI_ACCEL_STATUS_UNSUPPORTED;
            device->failed++;
            break;
    }

    if (command->status == AI_ACCEL_STATUS_OK ||
        command->status == AI_ACCEL_STATUS_SIMULATED) {
        device->completed++;
    }
}

static void sim_kick(ai_accel_device *device) {
    nex_sim_state *state = (nex_sim_state *)device->driver_data;
    if (!state) {
        return;
    }

    while (state->pending > 0) {
        ai_accel_command *command = state->slots[state->consumer];
        state->slots[state->consumer] = NULL;
        state->consumer = (state->consumer + 1u) % NEX_SIM_QUEUE_DEPTH;
        state->pending--;
        if (command) {
            sim_execute(device, command);
        }
    }
}

static void sim_poll(ai_accel_device *device) {
    sim_kick(device);
}

static bool sim_reset(ai_accel_device *device) {
    nex_sim_state *state = (nex_sim_state *)device->driver_data;
    if (!state) {
        return false;
    }

    for (u32 i = 0; i < NEX_SIM_QUEUE_DEPTH; ++i) {
        state->slots[i] = NULL;
    }
    state->producer = 0;
    state->consumer = 0;
    state->pending = 0;
    return true;
}

static const ai_accel_ops sim_ops = {
    .submit = sim_submit,
    .kick = sim_kick,
    .poll = sim_poll,
    .reset = sim_reset
};

bool nex_accel_sim_init(void) {
    for (u32 i = 0; i < NEX_SIM_QUEUE_DEPTH; ++i) {
        sim_state.slots[i] = NULL;
    }
    sim_state.producer = 0;
    sim_state.consumer = 0;
    sim_state.pending = 0;

    sim_device.id = 0;
    sim_device.name = "nexora-sim-accel";
    sim_device.kind = AI_DEVICE_NPU;
    sim_device.transport = AI_ACCEL_TRANSPORT_SIM;
    sim_device.device_mask = AI_DEVICE_GPU | AI_DEVICE_NPU;
    sim_device.vendor_id = 0;
    sim_device.device_id = 0;
    sim_device.pci_bus = 0;
    sim_device.pci_slot = 0;
    sim_device.pci_function = 0;
    sim_device.dma_address_bits = 64;
    sim_device.queue_depth = NEX_SIM_QUEUE_DEPTH;
    sim_device.online = true;
    sim_device.ops = &sim_ops;
    sim_device.driver_data = &sim_state;
    sim_device.submitted = 0;
    sim_device.completed = 0;
    sim_device.failed = 0;
    sim_device.bytes_moved = 0;

    return ai_accel_register(&sim_device);
}
