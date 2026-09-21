#ifndef NEXORA_KERNEL_PCI_H
#define NEXORA_KERNEL_PCI_H

#include <kernel/types.h>

#define PCI_MAX_DEVICES 128

typedef struct {
    u8 bus;
    u8 slot;
    u8 function;
    u16 vendor_id;
    u16 device_id;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    u8 revision;
    u8 header_type;
    u8 irq_line;
    u8 irq_pin;
    u32 bars[6];
} pci_device;

void pci_init(void);
u32 pci_config_read32(u8 bus, u8 slot, u8 function, u8 offset);
u16 pci_config_read16(u8 bus, u8 slot, u8 function, u8 offset);
u8 pci_config_read8(u8 bus, u8 slot, u8 function, u8 offset);
u32 pci_enumerate(void);
u32 pci_device_count(void);
const pci_device *pci_device_at(u32 index);
bool pci_is_processing_accelerator(const pci_device *dev);
bool pci_is_display_controller(const pci_device *dev);
void pci_print_summary(void);

#endif
