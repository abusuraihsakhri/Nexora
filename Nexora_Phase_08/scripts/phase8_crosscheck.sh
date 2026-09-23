#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

make phase8-host-test
make build/kernel.elf

nm build/kernel.elf | grep -q ' ai_inference_init$'
nm build/kernel.elf | grep -q ' ai_model_register$'
nm build/kernel.elf | grep -q ' ai_infer_submit$'
nm build/kernel.elf | grep -q ' ai_infer_form_batch$'
nm build/kernel.elf | grep -q ' ai_infer_plan_prefetch$'

grep -q 'Milestone 8 — Inference-oriented experiments' docs/ROADMAP.md
grep -q 'Phase 8' README.md

echo 'Phase 8 cross-check: PASS'
