# Changelog

## Phase 7 — Real Device Path

Added:

- x86-64 port-I/O helpers;
- PCI configuration-space enumeration;
- PCI accelerator/display candidate detection;
- experimental DMA buffer abstraction;
- generic accelerator registry and backend ABI;
- virtio-style simulated accelerator queue;
- DMA copy command with byte-for-byte verification;
- scheduler-to-accelerator work dispatch demonstration;
- accelerator telemetry;
- `make phase7-check` build/symbol gate;
- corrected the starter Multiboot2 section allocation/link layout so the header remains in the bootloader search window;
- independent Multiboot2 header/checksum validation in the cross-check script;
- Phase 7 architecture, validation, and integration documentation.

Deliberately not added:

- vendor GPU driver;
- CUDA/HIP/OpenCL kernel ABI;
- BAR programming;
- MSI/MSI-X;
- IOMMU;
- claim of real neural-network computation on physical accelerator hardware.
