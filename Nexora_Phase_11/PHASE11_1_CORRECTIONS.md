# Phase 11.1 Correction Ledger

This file records the findings from the post-Phase-11 cross-check and the corresponding correction.

| Finding | Phase 11.1 correction | Verification |
|---|---|---|
| Invalid policy effects could be inserted | `valid_effect()` is enforced by `ai_policy_add_grant()` and `ai_policy_validate()`; new `BAD_EFFECT` invariant code | deterministic regression test + source gate |
| `AI_POLICY_ANY_ID` could be used as a request object | concrete requests reject the wildcard sentinel with `DENY_INVALID_REQUEST` | deterministic regression test |
| 128 historical grants could permanently exhaust storage | inactive grants can be reclaimed into stable holes; allocation reuses holes; epoch bump reclaims all stale slots | full-table/revoke/reissue and epoch-reuse tests |
| Reclamation could risk pointer instability if implemented by compaction | active grants are never relocated; reclamation marks inactive slots free and trims only free tail slots | implementation review + invariant tests |
| Revoke/epoch actor looked like an authorization input | header/security docs now state that these are trusted kernel-management primitives and `actor_subject_id` is audit attribution only | source/document gate |
| `byte_limit` could be read as a cumulative budget | specified as a per-operation byte ceiling; repeated individually valid requests remain valid | deterministic semantic test + docs |
| GPU quota could be read as utilization metering | specified as a per-operation demand ceiling; trusted scheduler/device accounting must supply and enforce actual demand | docs + integration contract |
| SMP/concurrency model was implicit | engine is explicitly externally synchronized; authorization and privileged commitment must not race policy mutation | header + threat/integration docs |
| Sanitizers were reported but not rerun by the packaged gate | `make phase11-crosscheck` now executes ASan/UBSan and GCC `-fanalyzer` directly | cross-check script |

No change in this pass claims cryptographic capability integrity, IOMMU/GPU hardware isolation, cumulative quota accounting, distributed revocation convergence, or tamper-proof audit persistence.
