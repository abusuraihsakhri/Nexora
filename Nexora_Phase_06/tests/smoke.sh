#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
check_json() { python3 -c 'import json,sys; x=json.load(sys.stdin); assert x["schema"]=="nexora.bench.v1"; assert x["metrics"]'; }
bench/bin/alloc_latency --iterations 50 --warmup 5 --size 1024 --mode malloc | check_json
bench/bin/graph_sched --nodes 127 --rounds 2 --variant ready_queue | check_json
bench/bin/lifetime_peak --tensors 200 --max-lifetime 16 --variant lifetime_aware | check_json
bench/bin/zero_copy_ipc --iterations 5 --warmup 1 --size 1024 --variant shared_mem | check_json
bench/bin/deadline_tail --jobs 100 --policy edf | check_json
python3 scripts/ingest_nexora_serial.py results/example/nexora_serial.log | check_json
rm -rf results/test-run
python3 scripts/run_suite.py --quick --repetitions 1 --output results/test-run >/dev/null
python3 scripts/analyze.py results/test-run/raw.jsonl --out-dir results/test-run/analysis >/dev/null
test -s results/test-run/analysis/report.md
printf 'smoke tests passed\n'
