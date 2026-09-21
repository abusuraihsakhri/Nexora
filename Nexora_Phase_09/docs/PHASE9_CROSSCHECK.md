# Phase 9 cross-check matrix

| Requirement | Implementation | Validation |
|---|---|---|
| Remote GPU/resource object | `ai_dist_endpoint` | registry tests + validation |
| Remote tensor object | `ai_remote_tensor` | publish/replica test |
| Network transfer object | `ai_dist_transfer_request/plan` | direct/staged policy + tensor-reference tests |
| Collective object | `ai_dist_collective` | tree/ring collective tests |
| Topology-aware placement | `ai_dist_place` | placement + failover test |
| RDMA-style abstraction | transport-neutral link/plan types | RDMA preference routing test |
| Collective scheduling | `ai_dist_plan_collective` | tree/ring + directed-reduce + root-membership tests |
| Health/failure handling | endpoint/link health | failover + link-down test |
| Deadline awareness | transfer + placement deadline fields | deadline-miss test |
| No kernel libc allocation | fixed-size registry | static source scan |
| Additive Phase 8 integration | new header/source only | baseline kernel link + mirror check |

## Expected conclusion

All Phase 9 components are internally consistent if:

1. host tests pass;
2. the reference kernel ELF links with `distributed.c` enabled;
3. the static scan finds no malloc/free/socket/pthread dependency in the kernel module;
4. the stable SHA-256 manifest verifies with `sha256sum -c`;
5. sanitizer and adversarial tests pass.

The generated cross-check report is excluded from the stable hash manifest because its timestamp/output changes on every run.

This validates the Phase 9 module itself. It does **not** prove real multi-host networking performance because no physical transport driver is part of this milestone.
