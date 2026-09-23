#include <kernel/pci.h>
#include <kernel/printk.h>
#include <arch/x86_64/io.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PCI_VENDOR_NONE    0xFFFFu

static pci_device devices[PCI_MAX_DEVICES];
static u32 count = 0;

static u32 pci_address(u8 bus, u8 slot, u8 function, u8 offset) {
    return (1u << 31)
        | ((u32)bus << 16)
        | ((u32)slot << 11)
        | ((u32)function << 8)
        | ((u32)offset & 0xFCu);
}

void pci_init(void) {
    count = 0;
}

u32 pci_config_read32(u8 bus, u8 slot, u8 function, u8 offset) {
    io_out32(PCI_CONFIG_ADDRESS, pci_address(bus, slot, function, offset));
    return io_in32(PCI_CONFIG_DATA);
}

u16 pci_config_read16(u8 bus, u8 slot, u8 function, u8 offset) {
    u32 value = pci_config_read32(bus, slot, function, offset);
    return (u16)((value >> ((offset & 2u) * 8u)) & 0xFFFFu);
}

u8 pci_config_read8(u8 bus, u8 slot, u8 function, u8 offset) {
    u32 value = pci_config_read32(bus, slot, function, offset);
    return (u8)((value >> ((offset & 3u) * 8u)) & 0xFFu);
}

static bool function_present(u8 bus, u8 slot, u8 function) {
    return pci_config_read16(bus, slot, function, 0x00) != PCI_VENDOR_NONE;
}

static void capture_device(u8 bus, u8 slot, u8 function) {
    if (count >= PCI_MAX_DEVICES) {
        return;
    }

    pci_device *dev = &devices[count++];
    dev->bus = bus;
    dev->slot = slot;
    dev->function = function;
    dev->vendor_id = pci_config_read16(bus, slot, function, 0x00);
    dev->device_id = pci_config_read16(bus, slot, function, 0x02);
    dev->revision = pci_config_read8(bus, slot, function, 0x08);
    dev->prog_if = pci_config_read8(bus, slot, function, 0x09);
    dev->subclass = pci_config_read8(bus, slot, function, 0x0A);
    dev->class_code = pci_config_read8(bus, slot, function, 0x0B);
    dev->header_type = pci_config_read8(bus, slot, function, 0x0E);
    dev->irq_line = pci_config_read8(bus, slot, function, 0x3C);
    dev->irq_pin = pci_config_read8(bus, slot, function, 0x3D);

    for (u32 i = 0; i < 6; ++i) {
        dev->bars[i] = pci_config_read32(bus, slot, function, (u8)(0x10 + 4 * i));
    }
}

u32 pci_enumerate(void) {
    count = 0;

    for (u32 bus = 0; bus < 256; ++bus) {
        for (u32 slot = 0; slot < 32; ++slot) {
            if (!function_present((u8)bus, (u8)slot, 0)) {
                continue;
            }

            capture_device((u8)bus, (u8)slot, 0);
            u8 header = pci_config_read8((u8)bus, (u8)slot, 0, 0x0E);
            if ((header & 0x80u) == 0) {
                continue;
            }

            for (u32 function = 1; function < 8; ++function) {
                if (function_present((u8)bus, (u8)slot, (u8)function)) {
                    capture_device((u8)bus, (u8)slot, (u8)function);
                }
            }
        }
    }

    return count;
}

u32 pci_device_count(void) {
    return count;
}

const pci_device *pci_device_at(u32 index) {
    if (index >= count) {
        return NULL;
    }
    return &devices[index];
}

bool pci_is_processing_accelerator(const pci_device *dev) {
    return dev && dev->class_code == 0x12u;
}

bool pci_is_display_controller(const pci_device *dev) {
    return dev && dev->class_code == 0x03u;
}

void pci_print_summary(void) {
    kputs("PCI devices discovered: ");
    kprint_u64(count);
    kputs("\n");

    for (u32 i = 0; i < count; ++i) {
        const pci_device *dev = &devices[i];
        kputs("  PCI ");
        kprint_u64(dev->bus);
        kputs(":");
        kprint_u64(dev->slot);
        kputs(".");
        kprint_u64(dev->function);
        kputs(" vendor=");
        kprint_hex(dev->vendor_id);
        kputs(" device=");
        kprint_hex(dev->device_id);
        kputs(" class=");
        kprint_hex(dev->class_code);
        kputs(" subclass=");
        kprint_hex(dev->subclass);
        if (pci_is_processing_accelerator(dev)) {
            kputs(" [processing-accelerator]");
        } else if (pci_is_display_controller(dev)) {
            kputs(" [display-controller]");
        }
        kputs("\n");
    }
}
