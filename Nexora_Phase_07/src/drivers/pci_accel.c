#include <drivers/pci_accel.h>
#include <kernel/pci.h>
#include <kernel/memory.h>
#include <ai/accelerator.h>

static bool inert_submit(ai_accel_device *device, ai_accel_command *command) {
    (void)device;
    if (command) {
        command->status = AI_ACCEL_STATUS_UNSUPPORTED;
    }
    return false;
}

static void inert_kick(ai_accel_device *device) {
    (void)device;
}

static void inert_poll(ai_accel_device *device) {
    (void)device;
}

static bool inert_reset(ai_accel_device *device) {
    (void)device;
    return true;
}

static const ai_accel_ops inert_ops = {
    .submit = inert_submit,
    .kick = inert_kick,
    .poll = inert_poll,
    .reset = inert_reset
};

u32 pci_accel_discover(void) {
    u32 registered = 0;

    for (u32 i = 0; i < pci_device_count(); ++i) {
        const pci_device *pci = pci_device_at(i);
        if (!pci || (!pci_is_processing_accelerator(pci) && !pci_is_display_controller(pci))) {
            continue;
        }

        ai_accel_device *device = (ai_accel_device *)kalloc(sizeof(ai_accel_device), 16);
        device->id = 0;
        device->name = pci_is_processing_accelerator(pci)
            ? "pci-processing-accelerator"
            : "pci-display-controller";
        device->kind = pci_is_processing_accelerator(pci) ? AI_DEVICE_NPU : AI_DEVICE_GPU;
        device->transport = AI_ACCEL_TRANSPORT_PCI;
        device->device_mask = (u32)device->kind;
        device->vendor_id = pci->vendor_id;
        device->device_id = pci->device_id;
        device->pci_bus = pci->bus;
        device->pci_slot = pci->slot;
        device->pci_function = pci->function;
        device->dma_address_bits = 0;
        device->queue_depth = 0;
        device->online = false;
        device->ops = &inert_ops;
        device->driver_data = NULL;
        device->submitted = 0;
        device->completed = 0;
        device->failed = 0;
        device->bytes_moved = 0;

        if (ai_accel_register(device)) {
            registered++;
        }
    }

    return registered;
}
