#ifndef NEXORA_AI_ACCELERATOR_H
#define NEXORA_AI_ACCELERATOR_H

#include <kernel/types.h>
#include <ai/device.h>
#include <ai/work.h>

#define AI_ACCEL_MAX_DEVICES 16
#define AI_ACCEL_NAME_MAX    32

typedef enum {
    AI_ACCEL_TRANSPORT_SIM = 0,
    AI_ACCEL_TRANSPORT_PCI = 1,
    AI_ACCEL_TRANSPORT_MMIO = 2
} ai_accel_transport;

typedef enum {
    AI_ACCEL_CMD_NOP = 0,
    AI_ACCEL_CMD_COPY = 1,
    AI_ACCEL_CMD_WORK = 2
} ai_accel_command_type;

typedef enum {
    AI_ACCEL_STATUS_EMPTY = 0,
    AI_ACCEL_STATUS_QUEUED,
    AI_ACCEL_STATUS_RUNNING,
    AI_ACCEL_STATUS_OK,
    AI_ACCEL_STATUS_SIMULATED,
    AI_ACCEL_STATUS_UNSUPPORTED,
    AI_ACCEL_STATUS_FAILED
} ai_accel_status;

typedef struct {
    u64 command_id;
    ai_accel_command_type type;
    ai_accel_status status;
    u64 work_id;
    ai_op op;
    u64 src_phys;
    u64 dst_phys;
    u64 bytes;
    u32 flags;
} ai_accel_command;

struct ai_accel_device;

typedef struct {
    bool (*submit)(struct ai_accel_device *device, ai_accel_command *command);
    void (*kick)(struct ai_accel_device *device);
    void (*poll)(struct ai_accel_device *device);
    bool (*reset)(struct ai_accel_device *device);
} ai_accel_ops;

typedef struct ai_accel_device {
    u32 id;
    const char *name;
    ai_device_mask kind;
    ai_accel_transport transport;
    u32 device_mask;
    u16 vendor_id;
    u16 device_id;
    u8 pci_bus;
    u8 pci_slot;
    u8 pci_function;
    u8 dma_address_bits;
    u32 queue_depth;
    bool online;
    const ai_accel_ops *ops;
    void *driver_data;

    u64 submitted;
    u64 completed;
    u64 failed;
    u64 bytes_moved;
} ai_accel_device;

void ai_accel_system_init(void);
bool ai_accel_register(ai_accel_device *device);
u32 ai_accel_count(void);
ai_accel_device *ai_accel_at(u32 index);
ai_accel_device *ai_accel_find_for_mask(u32 device_mask);
bool ai_accel_submit(ai_accel_device *device, ai_accel_command *command);
void ai_accel_poll_all(void);
bool ai_accel_wait(ai_accel_device *device, ai_accel_command *command, u32 spin_limit);
bool ai_accel_submit_work(ai_accel_device *device, const ai_work_node *node, ai_accel_command *command);
const char *ai_accel_status_name(ai_accel_status status);
const char *ai_accel_transport_name(ai_accel_transport transport);
void ai_accel_print_summary(void);

#endif
