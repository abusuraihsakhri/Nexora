# NEXORA

## Neural EXecution Orchestration and Resource Architecture

### Architecture & Competitive Specification — Version 0.1

**Status:** Research architecture draft  
**Date:** September 2026  
**Primary target:** AI inference and heterogeneous AI execution  
**Long-term scope:** Training, agents, distributed AI, scientific/accelerated computing  

---

<div align="center">

[Interactive Documentation & Simulator](https://abusuraihsakhri.github.io/Nexora/) • [Executive Definition](#1-executive-definition) • [Core Primitives](#5-the-eight-core-nexora-primitives) • [Kernel Architecture](#6-kernel-architecture) • [Research Hypotheses](#11-first-five-research-hypotheses) • [Two-Implementation Strategy](#8-two-implementation-strategy)

</div>

---

# 1. Executive Definition

**Nexora is an AI-native operating-system architecture designed around semantic knowledge of AI workloads rather than conventional process/page/device abstractions alone.**

Its central hypothesis is:

> *An operating system that understands computation graphs, tensor lifetimes, model state, accelerator requirements, topology, data locality, deadlines, and resource capabilities can manage AI workloads more efficiently than an operating system that sees primarily processes, virtual pages, files, and devices.*

Nexora does **not** attempt to replace PyTorch, JAX, CUDA, ROCm, OpenXLA, Kubernetes, or existing model-serving frameworks initially.

Instead, Nexora introduces an OS-level semantic resource-management layer underneath or alongside them.

The target architecture is:

```text
               AI Applications / Agents
                         │
                  PyTorch / JAX
                         │
              Nexora Runtime Adapter
                         │
                  Nexora AI ABI
                         │
┌───────────────────────────────────────────────────┐
│                 NEXORA KERNEL                     │
│                                                   │
│  Work Graph       Tensor/State       Capability   │
│      │                 │                 │        │
│      └────────────┬────┴──────────┬─────┘        │
│                   │               │               │
│            Global Scheduler    Placement          │
│                   │               │               │
│              Resource Fabric / Topology           │
└───────────────────────┬───────────────────────────┘
                        │
          ┌─────────────┼──────────────┐
          │             │              │
         CPU           GPU            NPU
          │             │              │
         RAM           HBM         Accelerator RAM
          │             │              │
          └────── NVMe / NIC / Remote ─┘
```

---

# 2. Problem Statement

Contemporary AI software commonly crosses multiple independently managed layers:

```text
AI request
   ↓
model-serving runtime
   ↓
framework
   ↓
compiler/runtime
   ↓
CUDA / ROCm / PJRT
   ↓
Linux
   ↓
device driver
   ↓
accelerator
```

Each layer possesses different information:

```text
Model server knows:
    request, model, KV cache, batch

Framework knows:
    tensor, operation graph

Compiler knows:
    kernels, dependencies

GPU runtime knows:
    streams, buffers

Linux knows:
    process, thread, virtual page, file descriptor

Hardware knows:
    queues, memory, execution units
```

No single layer necessarily has complete information about:

```text
what is being computed
+
where its data resides
+
when the data will be reused
+
which accelerator should execute it
+
what the latency requirement is
+
what resources the workload is allowed to access
```

Nexora proposes to make enough of this semantic information visible to the operating-system resource manager to enable whole-system optimization.

---

# 3. Primary Research Question

The project should be evaluated against one central question:

> **Can semantic knowledge of AI workloads improve operating-system resource management enough to justify introducing new kernel abstractions?**

The answer must be determined experimentally.

Nexora is successful only if its abstractions measurably improve properties such as:

* tail latency;
* throughput;
* accelerator utilization;
* memory footprint;
* data movement;
* model cold-start latency;
* resource isolation;
* energy efficiency;
* scheduling overhead.

Architectural elegance alone is insufficient.

---

# 4. Design Principles

## 4.1 AI semantics are first-class information

The kernel must be capable of distinguishing between concepts such as:

```text
model weights
activation
KV cache
temporary workspace
dataset shard
checkpoint
persistent state
```

rather than treating all memory identically.

---

## 4.2 Data movement is a primary cost

Nexora should optimize:

```text
compute cost + data movement cost
```

rather than CPU/GPU execution time alone. Moving 10 GB of data may be more expensive than the computation subsequently performed on it.

---

## 4.3 Heterogeneous compute is normal

CPU, GPU, NPU, NIC, storage, and remote accelerators should appear inside one resource/topology model.

---

## 4.4 Work dependencies should influence resource management

The operating system should know:

```text
A → B → C
```

rather than observing three unrelated operations. Dependencies can influence scheduling, prefetching, memory reclamation, placement, synchronization, and admission control.

---

## 4.5 Policy and mechanism must remain separate

The kernel provides mechanisms and semantic information. Scheduling policies should remain replaceable:

```text
FIFO scheduler
deadline scheduler
locality scheduler
energy scheduler
LLM inference scheduler
```

should operate over the same underlying objects.

---

## 4.6 Compatibility is more valuable than purity

Nexora should initially exploit existing ecosystems:

* CUDA
* ROCm
* PJRT
* XLA
* PyTorch
* JAX
* vLLM
* SGLang

rather than attempting to replace entire ecosystems. PJRT is particularly relevant because OpenXLA explicitly defines it as a uniform hardware- and framework-independent device API with device-specific implementations behind the interface.

---

# 5. The Eight Core Nexora Primitives

These form the conceptual foundation of Nexora.

---

### Primitive 1 — `NXTensor`

A tensor becomes an OS-visible semantic data object.

Conceptual structure:

```c
NXTensor {
    id
    dtype
    shape
    size
    location
    owner
    lifetime
    mutability
    reuse_hint
    placement_constraints
    producers[]
    consumers[]
}
```

Example:

```text
Tensor #44
Type: BF16
Shape: [1, 4096, 4096]
Size: 32 MB
Class: KV_CACHE
Current location: GPU1.HBM
Allowed locations: GPU0.HBM, GPU1.HBM, HOST.RAM
Expected reuse: HIGH
```

The kernel does **not** need to understand neural-network mathematics. It needs enough metadata to make resource decisions.

**Intended benefit:** Better allocation, reclamation, placement, migration, sharing, and prefetching.

---

### Primitive 2 — `NXWork`

The fundamental schedulable semantic unit. Traditional systems primarily schedule threads/processes. Nexora introduces:

```c
NXWork {
    operation
    inputs[]
    outputs[]
    dependencies[]
    resource_requirements
    deadline
    priority
    cost_estimate
    placement_constraints
}
```

Example:

```text
NXWork 182
Operation: ATTENTION
Input: Q, K, V, KV_CACHE_88
Preferred device: GPU
Deadline: +8 ms
```

`NXWork` does not necessarily replace threads. It exists **above them as the AI scheduling abstraction**.

---

### Primitive 3 — `NXGraph`

A directed dependency graph composed of `NXWork` and data objects.

```text
Input
  │
  ▼
Embedding
  │
  ▼
Attention
  │
  ├──── KV cache
  │
  ▼
MLP
  │
  ▼
Sampling
```

The graph allows Nexora to determine:
* what can run
* what must wait
* what data will be needed next
* what memory can be freed
* what data should remain resident

This is a core distinction from conventional task scheduling.

---

### Primitive 4 — `NXState`

Some AI data has semantics beyond a generic tensor (e.g., models, KV caches, embedding caches, adapters, LoRA weights, session state, persistent agent state). Nexora defines `NXState` as a persistent semantic resource:

```c
NXState {
    id
    class
    backing_objects[]
    owner
    residency
    persistence
    sharing_policy
    lifecycle
}
```

Example:

```text
Model: Llama-X
Weights: 70 GB
Placement: GPU0 + GPU1
State: persistent
```

Model residency becomes an operating-system resource-management decision.

---

### Primitive 5 — `NXResource`

Every compute/storage resource receives a common representation:

```text
NXResource
    │
    ├── CPU
    ├── GPU
    ├── NPU
    ├── HBM
    ├── RAM
    ├── NVMe
    ├── NIC
    └── remote accelerator
```

```c
NXResource {
    id
    type
    topology
    capacity
    available_capacity
    bandwidth
    latency
    capabilities
    memory_domains[]
}
```

The goal is to make heterogeneous compute and memory **queryable through one unified resource model**.

---

### Primitive 6 — `NXTopology`

Hardware relationships become explicitly represented:

```text
                 CPU0
                  │
                 RAM0
                  │
              PCIe Root
              /       \
           GPU0       GPU1
            │           │
           HBM0        HBM1
              \        /
                 NIC
```

Nexora tracks approximate latency, bandwidth, NUMA distance, PCIe topology, accelerator interconnects, memory accessibility, and network locality. Placement can then minimize movement.

---

### Primitive 7 — `NXCapability`

Security uses explicit resource authority:

```text
Agent A
CAN:
    read Dataset X
    execute Model M
    use GPU0
    access network endpoint Y
CANNOT:
    modify Dataset X
    access Model N
    access arbitrary files
```

```c
NXCapability {
    subject
    resource
    rights
    constraints
    expiration
}
```

Enables fine-grained delegation with constraints such as maximum GPU allocation, network destinations, model identifiers, tensor permissions, time windows, and memory quotas.

---

### Primitive 8 — `NXScheduler`

The Nexora scheduler operates over:

```text
WorkGraph + Tensor/State + Resource availability + Topology + Capabilities + deadlines
```

A simplified scheduling cost function:

$$\text{TOTAL COST} = \text{execution time} + \text{data transfer cost} + \text{queue delay} + \text{memory pressure penalty} + \text{deadline penalty} + \text{topology penalty}$$

For candidate device `D`, $\text{Cost}(\text{work}, D)$ is calculated. The lowest acceptable-cost placement wins.

---

# 6. Kernel Architecture

The kernel is divided into two distinct planes:

```text
                  Applications
                       │
                Nexora Runtime
                       │
                Nexora Syscalls
                       │
        ┌──────────────┴──────────────┐
        │                             │
 [ SEMANTIC PLANE ]            [ MECHANISM PLANE ]
        │                             │
   NXWorkGraph                   CPU scheduler
   NXTensor                      VM
   NXState                       DMA
   NXCapability                  interrupts
        │                        drivers
        └──────────────┬──────────────┘
                       │
                  NXScheduler
                       │
                  NXResource
                       │
                    Hardware
```

---

# 7. Initial Nexora ABI

The initial experimental ABI remains intentionally minimal:

```c
nx_tensor_create()
nx_tensor_destroy()

nx_tensor_map()
nx_tensor_unmap()

nx_state_create()
nx_state_release()

nx_graph_create()
nx_graph_destroy()

nx_work_add()
nx_work_submit()

nx_work_wait()

nx_resource_query()

nx_cap_create()
nx_cap_delegate()
nx_cap_revoke()
```

---

# 8. Two-Implementation Strategy

Developing only a bare-metal kernel would make accelerator experimentation unnecessarily difficult. Nexora therefore exists in two implementations:

### 8.1 Nexora-RK (Research Kernel)

```text
Hardware / QEMU
       │
       ▼
   Nexora-RK
```

* Purpose: Experiment with new kernel abstractions, memory-management research, capability architecture, scheduling research, and OS publications.

### 8.2 Nexora-X (Experimental Runtime / Co-kernel)

```text
PyTorch / JAX / AI application
              │
              ▼
           Nexora-X
              │
     semantic scheduler
              │
 ┌────────────┼─────────────┐
 │            │             │
CUDA        ROCm          PJRT
 │            │             │
 └──────── Linux kernel ─────┘
              │
           Hardware
```

* Purpose: Real NVIDIA/AMD/accelerator testing, rapid experiments, production-scale benchmarks, and framework integration. Successful mechanisms migrate into Nexora-RK.

---

# 9. Competitive Boundary

Nexora establishes explicit boundaries relative to existing projects:

* **Linux:** Provides mature processes, threads, virtual memory, filesystems, and drivers. Nexora’s difference is semantic AI resource management rather than reproducing general device drivers.
* **LithOS:** Focused on fine-grained spatial scheduling within GPUs. Nexora targets whole-system (CPU + GPU + NPU + memory + storage + network) semantic scheduling.
* **NVIDIA Dynamo:** Distributed generative-AI serving and cache routing. Dynamo acts at the inference-routing level; Nexora provides the lower-level semantic resource mechanism. Dynamo could eventually run on Nexora.
* **NVIDIA Run:ai:** Orchestrates cluster workload allocation. Run:ai handles cluster management; Nexora handles machine/resource execution architecture.
* **OpenXLA / PJRT:** Uniform hardware-independent device API. Nexora integrates with PJRT rather than duplicating it.
* **AIOS:** Places an agent kernel above an underlying OS kernel. Nexora targets the resource-management kernel layer itself.

---

# 10. Deliberate Scope Boundaries

Nexora focuses on the operating-system layer where AI workload semantics can influence resource management. It is designed to complement, rather than duplicate, mature ecosystems.

* **Accelerator stacks:** CUDA, ROCm, PJRT, and vendor drivers remain the execution substrate for practical hardware experiments.
* **ML frameworks:** PyTorch and JAX remain the programming and model-execution environments above Nexora.
* **Compilers:** XLA, MLIR, Triton, and related compiler systems remain responsible for lowering and kernel generation.
* **Cluster orchestration:** Kubernetes and distributed serving platforms operate above Nexora's initial machine-level resource-management scope.
* **Model serving:** Systems such as vLLM and Dynamo are workloads and integration targets, not components Nexora seeks to reproduce.
* **POSIX compatibility:** Nexora prioritizes the mechanisms required to test AI-native OS abstractions rather than broad legacy API coverage.
* **Device breadth:** Early hardware work concentrates on a narrow set of representative interfaces so that semantic scheduling, memory, topology, and isolation can be measured rigorously.

---

# 11. First Five Research Hypotheses

* **H1 — Semantic tensor lifetime:** Producer/consumer graph awareness allows early reclamation of temporary tensors, reducing peak memory relative to lifetime-blind allocation.
* **H2 — Locality-aware scheduling:** Graph and topology co-scheduling reduces CPU↔GPU and GPU↔GPU data transfers while preserving throughput.
* **H3 — Graph-aware scheduling:** WorkGraph dependency knowledge enables better scheduling (throughput, p95/p99 tail latency) than independent FIFO tasks.
* **H4 — Shared tensor handles:** Capability-protected shared physical backing eliminates redundant copying across isolated workloads.
* **H5 — Persistent model state:** Treating model weights and KV caches as OS-managed persistent resources reduces cold-start latency and time-to-first-token.

---

# 12. First Target Workload

> **Multi-model LLM inference on one heterogeneous machine.**

```text
         Requests
       /    |     \
      A     B      C
      │     │      │
   Model1 Model2 Model1
       \    |     /
        Nexora Graph
             │
      NXScheduler
             │
 ┌───────────┼────────────┐
 │           │            │
CPU0       GPU0          GPU1
 │           │            │
RAM        HBM0          HBM1
```

Exercises dynamic requests, strict latency deadlines, large model state, KV caches, high memory pressure, multiple accelerators, CPU/GPU coordination, model residency, and data movement.

---

# 13. Benchmark Baselines & Metrics

### Baselines
* Linux + PyTorch
* Linux + vLLM
* Linux + NVIDIA Dynamo
* Linux + CUDA
* Linux + PJRT/OpenXLA

### Core Metrics
* **Inference:** TTFT, time per output token, requests/second, p50/p95/p99 latency, accelerator utilization, memory footprint, KV-cache hit rate, cold-start time.
* **OS Architecture:** Scheduler overhead, context-switch latency, peak memory, bytes transferred, allocation latency, DMA setup overhead, capability-check overhead.
* **Efficiency:** Requests/joule, tokens/joule, GPU-hours/request.

---

# 14. Success Criteria

* **Memory:** $\ge 15\%$ reduction in peak memory for defined workloads.
* **Data movement:** $\ge 20\%$ reduction in avoidable device transfers.
* **Tail latency:** $\ge 10\%$ reduction in p99 without throughput loss.
* **Model residency:** Measurable cold-start reduction under multi-model workloads.

---

# 15. Development Sequence & Repository Architecture

```text
Phase 0  ──►  Boot, console, early memory, NXTensor/NXWork prototypes
Phase 1  ──►  Kernel substrate (physical allocator, VM, interrupts, APIC, heap)
Phase 2  ──►  Semantic memory (NXTensor backing, ref counting, auto-reclamation)
Phase 3  ──►  Resource fabric (NXResource, NXTopology)
Phase 4  ──►  Scheduler laboratory (interchangeable policies: FIFO, priority, locality, graph)
Phase 5  ──►  Nexora-X (Linux-hosted prototype for real GPUs via CUDA/ROCm/PJRT)
Phase 6  ──►  Shared tensor mechanism (capability-controlled shared backing)
Phase 7  ──►  Persistent model state (NXModel, NXKVState)
Phase 8  ──►  Real inference experiments (vLLM integration)
```

### Repository Layout

```text
nexora/
├── kernel/        # Core microkernel: arch, memory, scheduler, capability, syscall
├── semantic/      # Semantic plane: tensor, work, graph, state, topology
├── runtime/       # User runtime adapters (C, Python, Rust)
├── backends/      # Execution targets: simulator, pjrt, cuda, rocm
├── experiments/   # Benchmark experiments: memory, scheduling, locality, inference
├── benchmarks/    # Standardized evaluation test harnesses
├── docs/          # Interactive GitHub Pages documentation and simulator
└── tools/         # Analysis and qualification utilities
```

---

# 16. Long-Term Architecture

```text
                  APPLICATIONS / AGENTS
                           │
                  Nexora Runtime API
                           │
                     NXExecution
                           │
       ┌───────────────────┼────────────────────┐
       │                   │                    │
    NXGraph             NXState            NXCapability
       │                   │                    │
       └───────────────────┼────────────────────┘
                           │
                      NXScheduler
                           │
                    NXResource Fabric
                           │
       ┌─────────────┬─────┼──────┬────────────┐
       │             │     │      │            │
      CPU           GPU   NPU    NIC          NVMe
       │             │     │      │            │
      RAM           HBM   RAM   Network       Data
       └─────────────┴─────┴──────┴────────────┘
```

> **Work + State + Resources + Relationships** rather than merely **Process + Pages + Devices**.

---

# 17. Defensible Research Position & Project Rules

Nexora does not claim to be "the first AI operating system." Rather:

> **Nexora investigates whether exposing AI workload semantics directly to operating-system resource management enables more efficient whole-system execution across heterogeneous compute, memory, storage, and networking resources.**

### The Four Project Rules

Every kernel abstraction must answer:
1. *What existing problem does this solve?*
2. *Why can't the current application/runtime layer solve it adequately?*
3. *Why should the operating system know about it?*
4. *What measurable improvement results?*

---

# 18. Immediate Engineering Objective

The immediate focus is closing the semantic-memory loop:

$$\text{NXGraph} \longrightarrow \text{Producer/Consumer DAG} \longrightarrow \text{NXTensor} \longrightarrow \text{Semantic Lifetime} \longrightarrow \text{Physical Allocator} \longrightarrow \text{Auto Reclamation}$$

> **Can Nexora's knowledge of graph-level tensor lifetimes reduce peak memory usage compared with a conventional lifetime-blind allocation strategy?**

---

## 🌐 Interactive Documentation & Simulator

An interactive documentation suite and real-time state machine simulator is available at:

### 🔗 [https://abusuraihsakhri.github.io/Nexora/](https://abusuraihsakhri.github.io/Nexora/)

* **Architecture Overview:** Interactive breakdown of the 8 core primitives, dual planes, and research roadmap.
* **Boot State-Machine Simulator:** Interactive visual simulator modeling long-mode setup, privilege transitions, and syscall round-trips.

