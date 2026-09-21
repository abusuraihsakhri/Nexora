# Phase 3 Step 8 — Locality + Device Constraints

## Goal

Step 8 turns the device mask introduced earlier into a concrete placement model.
Nexora can now reason about *which specific CPU/GPU/NPU object* should execute a
ready work node, how much input/output movement that choice implies, and what the
combined execution + transfer estimate is.

This is still a simulator embedded in the kernel. It is intentionally separated
from real PCI discovery, DMA engines, GPU drivers, and accelerator command queues.

## 1. Concrete device identity

`NX_OBJECT_DEVICE` is now used for persistent device objects. Device IDs are
therefore generation-checked kernel handles rather than anonymous class bits.

The core descriptor is:

```c
typedef struct {
    const char *name;
    ai_device_mask kind;
    u64 memory_bytes;
    u32 numa_node;
    u64 compute_ops_per_sec;
    u64 memory_bandwidth_bytes_per_sec;
} ai_device_desc;
```

The current boot-time reference topology creates:

```text
cpu0
  |
  +------ gpu0
  |
  +------ npu0

gpu0 ----- npu0
```

The numerical capability/link values are synthetic model parameters. They are
not claims about the machine running Nexora.

## 2. Device classes versus device identities

A work node keeps its hard device-class mask:

```c
node->device_mask
```

For example:

```text
CPU | GPU
```

means that both classes are legal execution targets. Step 8 adds concrete IDs:

```c
node->preferred_device;
node->selected_device;
```

`preferred_device` is optional. `selected_device` is written when the scheduler
commits a dispatch.

The class mask remains a hard constraint. A preferred device outside the mask is
rejected.

## 3. Tensor locality

Every tensor now records:

```c
tensor->preferred_device;
tensor->resident_device;
```

The default preferred device is inferred from the tensor's logical location when
that concrete device exists:

```text
AI_LOC_CPU_RAM -> default CPU
AI_LOC_GPU_HBM -> default GPU
AI_LOC_NPU_MEM -> default NPU
AI_LOC_NVME    -> default storage device, if one exists
AI_LOC_REMOTE  -> default NIC/remote endpoint, if one exists
```

`resident_device` becomes valid when backing is attached. Unbinding storage clears
resident identity.

The current physical allocator is still the bootstrap CPU arena. Therefore GPU/NPU
residency is semantic/emulated and the backing memory remains explicitly marked
`NX_MEMORY_FLAG_EMULATED_DEVICE`.

## 4. Topology links

A direct device link contains:

```c
typedef struct {
    ai_device_id_t from;
    ai_device_id_t to;
    u64 bandwidth_bytes_per_sec;
    u64 latency_ns;
    bool valid;
} ai_device_link;
```

Transfer time is estimated as:

```text
fixed link latency + ceil(bytes / effective bandwidth)
```

The arithmetic is overflow checked and avoids requiring 128-bit runtime helpers in
the freestanding kernel.

Step 8 only supports direct routes. Multi-hop route planning belongs to a later
resource/topology phase.

## 5. Placement evaluation

The new API:

```c
ai_placement_status ai_placement_evaluate(
    const ai_work_node *node,
    ai_device_id_t device_id,
    ai_dispatch_choice *out_choice
);
```

calculates a placement record containing:

```c
work
device_id
input_transfer_bytes
output_transfer_bytes
transfer_ns
execution_ns
total_ns
input_transfers
output_transfers
preferred_device
```

A candidate is rejected when:

- the concrete device does not exist,
- its class is outside `device_mask`,
- an input/output transfer has no modeled route,
- cost arithmetic overflows.

## 6. Transfer cost

Input locality is evaluated from:

```text
resident_device
    else preferred_device
```

If the source differs from the candidate device, the tensor's logical byte count
is charged over the topology link.

Output tensors are treated similarly. Their preferred destination is used as the
post-execution target. If compute happens elsewhere, the output transfer is added
to the placement estimate.

This lets placement distinguish cases such as:

```text
CPU input + GPU weight + GPU output
```

where running on the GPU transfers only the CPU input, while running on the CPU
would require moving GPU-resident data and moving the result back to the GPU.

## 7. Execution estimate

When device capability data and work estimates are available:

```text
compute_ns = estimated_ops / device_compute_rate
memory_ns  = estimated_traffic / device_memory_bandwidth
execution_ns = max(compute_ns, memory_ns)
```

This is a deliberately small roofline-like model. It is not a hardware performance
model.

If those inputs are unavailable, `estimated_duration_ns` remains the fallback.

The current placement total is:

```text
total_ns = execution_ns + transfer_ns
```

Memory pressure, queue delay, deadline penalty, and contention are deferred to later
scheduler phases.

## 8. Preferred device semantics

A preferred work device does not add an arbitrary numeric penalty to every other
device. Instead, it is used as a deterministic tie-break when candidate total cost
is equal.

This keeps the cost model interpretable:

```text
measured/modelled cost first
explicit preference second
stable device ID last
```

## 9. Scheduler integration

The scheduler now exposes:

```c
ai_scheduler_pick_dispatch(...)
ai_scheduler_mark_running_on(...)
```

A dispatch choice is a pair:

```text
(work node, concrete device)
```

Across different ready work nodes, the existing scheduling intent still dominates:

```text
priority
  -> deadline presence/value
  -> QoS
  -> supplied duration metadata
  -> locality placement cost
  -> stable object ID
```

This intentionally prevents locality from silently overriding an explicit high
priority or deadline policy.

## 10. Output residency commit

When a scheduler-completed work node has backed outputs, their semantic residency is
committed to the output preferred destination, or to the selected execution device
when no preferred destination exists.

This models the result of the planned transfer. It does not yet perform DMA or copy
physical bytes between devices.

## 11. Host tests

`tests/locality_placement_test.c` verifies:

- creation of synthetic CPU/GPU/NPU device objects,
- direct CPU->GPU transfer-time estimation,
- tensor default preferred/resident identities,
- mixed-locality GPU placement,
- CPU-local placement when transfer cost dominates,
- preferred-device tie breaking,
- hard device-mask enforcement,
- selected-device recording,
- scheduler work/device dispatch,
- output residency commit,
- device statistics.

Run:

```bash
make test-placement
```

or the complete suite:

```bash
make test
```

## 12. Current limitations

Step 8 deliberately does not implement:

- PCI/ACPI device discovery,
- real GPU/NPU drivers,
- DMA/copy execution,
- page migration,
- device memory allocators,
- queue occupancy/contention,
- multi-hop routing,
- power/thermal policy,
- NUMA distance matrices,
- memory-pressure penalties.

The point of this step is to establish a stable kernel representation and a
reproducible placement decision before real hardware complexity is introduced.

## 13. Why this matters for Nexora

The scheduler can now distinguish these two statements:

```text
"this work can run on a GPU"
```

and:

```text
"this work should run on gpu0 because most of its data is already there and the
estimated transfer + execution cost is lower than the CPU alternative"
```

That is the first concrete device-placement decision in the Nexora kernel model.
