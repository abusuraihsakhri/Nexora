#ifndef NEXORA_DRIVERS_PCI_ACCEL_H
#define NEXORA_DRIVERS_PCI_ACCEL_H

#include <kernel/types.h>

/* Registers PCI processing/display devices as discovered-but-inert backends. */
u32 pci_accel_discover(void);

#endif
