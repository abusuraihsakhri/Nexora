# AIKernel Design Principles

## 1. AI workloads are not generic processes

The kernel should model computation in terms of **work units and dependency graphs**, while retaining enough conventional process machinery later for compatibility.

## 2. Data movement is a primary cost

Tensor placement, lifetime, reuse, and transfer cost must be explicit. The kernel should be able to distinguish:

- persistent weights
- KV cache
- activations
- temporary workspace
- dataset shards
- checkpoints

## 3. Heterogeneous compute is normal

CPU, GPU, NPU, NIC, storage, and remote accelerators should be represented through a unified resource model rather than as unrelated special cases.

## 4. Security should use capabilities

AI agents and services should receive explicit resource capabilities instead of broad ambient authority.

## 5. The graph is operational, not decorative

Dependencies should influence scheduling, prefetch, reclamation, placement, and synchronization.

## 6. Prove every abstraction experimentally

Any feature that does not improve a measurable property should be challenged.

Primary metrics:

- latency
- throughput
- memory bandwidth
- memory footprint
- transfer volume
- accelerator utilization
- scheduling overhead
- tail latency
- energy per unit of work

## 7. Avoid premature hardware-driver work

The early kernel should use QEMU and simulated devices where possible. Hardware support is expensive and should follow proof of architectural value.
