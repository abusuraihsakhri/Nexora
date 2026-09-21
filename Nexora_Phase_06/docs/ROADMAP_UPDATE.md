# Roadmap Update

Original Milestone/Phase 6 objective: **Host-side comparison harness**.

Status in this package: **implemented and locally verified**.

Implemented comparisons:

- allocation latency
- graph scheduling overhead
- tensor lifetime memory peak
- zero-copy IPC
- deadline tail latency

The phase creates the experimental apparatus but does not by itself establish a Nexora performance advantage. That requires matched telemetry from the cumulative Phase-5 kernel implementation.

Next roadmap phase: **Phase 7 — Real device path**, beginning with a virtio-style simulated accelerator before PCI/DMA and real accelerator drivers.
