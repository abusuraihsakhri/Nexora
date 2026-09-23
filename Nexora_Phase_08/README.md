# Nexora AIKernel — Phase 8

## Inference-Oriented Resource Management

Phase 8 explores how an AI-native operating system can manage inference as a semantic workload rather than as an opaque collection of threads, allocations, and device calls.

The central idea is that the operating system can make better resource decisions when it can directly represent model residency, KV-cache state, request deadlines, batching opportunities, prefetch intent, and memory pressure.

## Core Concepts

### Persistent model residency

Model weights are represented as long-lived resources with explicit residency state and preferred execution resources. This allows model placement and eviction to become system-level policy decisions rather than incidental consequences of application allocation.

### KV-cache state

Session-specific KV state is represented explicitly, including ownership, size, memory location, pinning, and recency information. This creates a basis for future reuse, migration, eviction, and distributed-state policies.

### Memory-pressure-aware admission

Inference requests carry estimated working-memory requirements. Admission considers a bounded memory budget and configurable high-watermark so the system can reject work before oversubscription becomes uncontrolled.

### Deadline-aware dynamic batching

Queued requests retain model identity, token counts, enqueue time, and deadlines. Compatible requests can be grouped into bounded batches, with earlier deadlines receiving priority.

### Tensor prefetch planning

The inference layer can express movement intent before a tensor is required, including source, destination, size, and need-by time. This provides a semantic hook for future topology- and bandwidth-aware transfer planning.

## Semantic Flow

```text
Inference request
       │
       ├── model residency
       ├── KV-cache state
       ├── memory estimate
       └── deadline
       │
       ▼
Admission control
       │
       ▼
Dynamic batch formation
       │
       ├── prefetch intent
       └── placement constraints
       │
       ▼
Accelerator / resource path
```

## Research Questions

Phase 8 is intended to support measurable experiments around:

- whether explicit model residency reduces cold-start overhead;
- whether OS-visible KV state improves reuse and memory efficiency;
- whether deadline-aware batching improves tail latency under mixed workloads;
- whether memory-aware admission prevents pathological overload;
- whether semantic prefetch reduces avoidable data-movement stalls.

## Current Implementation Scope

The implementation provides bounded kernel-side metadata and policy primitives for model objects, KV state, inference requests, batching, admission control, memory accounting, and prefetch planning.

It is a research control plane rather than a replacement for vLLM, CUDA, ROCm, PJRT, model compilers, tokenizers, or production accelerator drivers. Those systems remain complementary execution and integration layers.

## Position in Nexora

Phase 8 connects the earlier resource/device foundations to the later distributed-resource architecture:

```text
Semantic tensors + work graphs
            │
            ▼
Resource / accelerator path
            │
            ▼
Inference semantics
(model + KV + request + batch)
            │
            ▼
Distributed placement and transfer
```

The result is a clearer operating-system abstraction for studying multi-model inference under memory pressure, heterogeneous compute, data locality, and latency constraints.
