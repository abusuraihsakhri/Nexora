# Changelog

## Phase 8 — inference-oriented experiments

- Added persistent model-residency objects.
- Added KV-cache/session residency metadata.
- Added memory-pressure-aware request admission.
- Added deadline-aware dynamic batching.
- Added tensor prefetch planning.
- Added deterministic host tests and a Phase 8 cross-check target.
- Restored the missing Phase 8 repository directory from the Phase 7 baseline plus the repository-native Milestone 8 and Phase 9 integration contracts.


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
