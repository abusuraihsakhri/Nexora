#!/usr/bin/env python3
"""Nexora Phase 16 release-gate validator.

Input CSV columns:
run_id,test,success,latency_ns,peak_memory_bytes,bytes_copied,
isolation_violations,unrecovered_faults,telemetry_drops

Performance comparisons are performed PER TEST/SCENARIO; heterogeneous workloads
are never pooled into one latency distribution. ``bytes_copied`` means payload bytes,
not bookkeeping metadata bytes.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

REQUIRED_COLUMNS = {
    "run_id", "test", "success", "latency_ns", "peak_memory_bytes",
    "bytes_copied", "isolation_violations", "unrecovered_faults", "telemetry_drops"
}
GATE_KEYS = {
    "success_rate_min",
    "isolation_violations_max",
    "unrecovered_faults_max",
    "telemetry_drops_max",
    "median_latency_regression_pct_max",
    "p99_latency_regression_pct_max",
    "peak_memory_regression_pct_max",
    "zero_copy_payload_bytes_copied_max",
}


@dataclass(frozen=True)
class Row:
    run_id: str
    test: str
    success: bool
    latency_ns: int
    peak_memory_bytes: int
    bytes_copied: int
    isolation_violations: int
    unrecovered_faults: int
    telemetry_drops: int


def parse_bool(value: str) -> bool:
    value = value.strip().lower()
    if value in {"1", "true", "yes", "pass"}:
        return True
    if value in {"0", "false", "no", "fail"}:
        return False
    raise ValueError(f"invalid boolean: {value!r}")


def nonnegative(name: str, value: str) -> int:
    try:
        parsed = int(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(f"{name} must be an integer, got {value!r}") from exc
    if parsed < 0:
        raise ValueError(f"{name} must be non-negative, got {parsed}")
    return parsed


def load_csv(path: Path) -> list[Row]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        fields = set(reader.fieldnames or [])
        missing = REQUIRED_COLUMNS - fields
        if missing:
            raise ValueError(f"{path}: missing columns: {sorted(missing)}")

        rows: list[Row] = []
        seen_ids: set[tuple[str, str]] = set()
        for line_no, raw in enumerate(reader, start=2):
            test = (raw.get("test") or "").strip()
            run_id = (raw.get("run_id") or "").strip()
            if not test:
                raise ValueError(f"{path}:{line_no}: blank test")
            if not run_id:
                raise ValueError(f"{path}:{line_no}: blank run_id")
            key = (test, run_id)
            if key in seen_ids:
                raise ValueError(
                    f"{path}:{line_no}: duplicate run_id {run_id!r} for test {test!r}"
                )
            seen_ids.add(key)

            rows.append(Row(
                run_id=run_id,
                test=test,
                success=parse_bool(raw["success"]),
                latency_ns=nonnegative("latency_ns", raw["latency_ns"]),
                peak_memory_bytes=nonnegative("peak_memory_bytes", raw["peak_memory_bytes"]),
                bytes_copied=nonnegative("bytes_copied", raw["bytes_copied"]),
                isolation_violations=nonnegative("isolation_violations", raw["isolation_violations"]),
                unrecovered_faults=nonnegative("unrecovered_faults", raw["unrecovered_faults"]),
                telemetry_drops=nonnegative("telemetry_drops", raw["telemetry_drops"]),
            ))

    if not rows:
        raise ValueError(f"{path}: no data rows")
    return rows


def _string_list(cfg: dict[str, object], key: str) -> list[str]:
    value = cfg.get(key)
    if not isinstance(value, list) or not value:
        raise ValueError(f"configuration key {key!r} must be a non-empty list")
    if any(not isinstance(item, str) or not item.strip() for item in value):
        raise ValueError(f"configuration key {key!r} contains an invalid scenario name")
    normalized = [item.strip() for item in value]
    if len(set(normalized)) != len(normalized):
        raise ValueError(f"configuration key {key!r} contains duplicates")
    return normalized


def load_config(path: Path) -> dict[str, object]:
    cfg = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(cfg, dict):
        raise ValueError("gate configuration must be a JSON object")
    if cfg.get("schema_version") != 1:
        raise ValueError(f"unsupported gate schema_version: {cfg.get('schema_version')!r}")

    minimum_runs = cfg.get("minimum_runs_per_test")
    if not isinstance(minimum_runs, int) or isinstance(minimum_runs, bool) or minimum_runs < 2:
        raise ValueError("minimum_runs_per_test must be an integer >= 2")

    required = _string_list(cfg, "required_scenarios")
    zero_copy = _string_list(cfg, "zero_copy_scenarios")
    if not set(zero_copy).issubset(required):
        raise ValueError("zero_copy_scenarios must be a subset of required_scenarios")

    gates = cfg.get("gates")
    if not isinstance(gates, dict):
        raise ValueError("configuration key 'gates' must be an object")
    missing = GATE_KEYS - set(gates)
    if missing:
        raise ValueError(f"gate configuration missing keys: {sorted(missing)}")
    for key in GATE_KEYS:
        value = gates[key]
        if not isinstance(value, (int, float)) or isinstance(value, bool) or value < 0:
            raise ValueError(f"gate {key!r} must be a non-negative number")
    if float(gates["success_rate_min"]) > 1.0:
        raise ValueError("success_rate_min cannot exceed 1.0")
    return cfg


def percentile(values: Iterable[int], p: float) -> float:
    xs = sorted(values)
    if not xs:
        return math.nan
    if len(xs) == 1:
        return float(xs[0])
    rank = (len(xs) - 1) * p
    lo, hi = math.floor(rank), math.ceil(rank)
    if lo == hi:
        return float(xs[lo])
    frac = rank - lo
    return xs[lo] * (1 - frac) + xs[hi] * frac


def pct_regression(candidate: float, baseline: float) -> float:
    if baseline == 0:
        return 0.0 if candidate == 0 else math.inf
    return (candidate - baseline) * 100.0 / baseline


def summarize(rows: list[Row]) -> dict[str, dict[str, float]]:
    grouped: dict[str, list[Row]] = defaultdict(list)
    for row in rows:
        grouped[row.test].append(row)
    result: dict[str, dict[str, float]] = {}
    for test, group in sorted(grouped.items()):
        result[test] = {
            "runs": float(len(group)),
            "success_rate": sum(r.success for r in group) / len(group),
            "median_latency_ns": float(statistics.median(r.latency_ns for r in group)),
            "p99_latency_ns": percentile((r.latency_ns for r in group), 0.99),
            "peak_memory_bytes": float(max(r.peak_memory_bytes for r in group)),
            "bytes_copied_max": float(max(r.bytes_copied for r in group)),
        }
    return result


def totals(rows: list[Row]) -> dict[str, int]:
    return {
        "rows": len(rows),
        "isolation_violations": sum(r.isolation_violations for r in rows),
        "unrecovered_faults": sum(r.unrecovered_faults for r in rows),
        "telemetry_drops": sum(r.telemetry_drops for r in rows),
    }


def evaluate(
    baseline_rows: list[Row],
    candidate_rows: list[Row],
    cfg: dict[str, object],
) -> dict[str, object]:
    baseline = summarize(baseline_rows)
    candidate = summarize(candidate_rows)
    baseline_totals = totals(baseline_rows)
    candidate_totals = totals(candidate_rows)
    gates = cfg["gates"]
    assert isinstance(gates, dict)
    minimum_runs = int(cfg["minimum_runs_per_test"])
    required = set(cfg["required_scenarios"])
    zero_copy = set(cfg["zero_copy_scenarios"])

    checks: list[dict[str, object]] = []

    def add(name: str, ok: bool, detail: str, test: str | None = None) -> None:
        item: dict[str, object] = {"name": name, "pass": ok, "detail": detail}
        if test is not None:
            item["test"] = test
        checks.append(item)

    base_tests = set(baseline)
    cand_tests = set(candidate)
    add(
        "scenario_set_match",
        base_tests == cand_tests,
        f"baseline={sorted(base_tests)} candidate={sorted(cand_tests)}",
    )
    missing_base = sorted(required - base_tests)
    missing_cand = sorted(required - cand_tests)
    add(
        "required_scenarios_present",
        not missing_base and not missing_cand,
        f"baseline_missing={missing_base} candidate_missing={missing_cand}",
    )

    for test in sorted(base_tests | cand_tests):
        if test not in baseline or test not in candidate:
            continue
        b, c = baseline[test], candidate[test]
        add(
            "baseline_minimum_runs_per_test",
            b["runs"] >= minimum_runs,
            f"{int(b['runs'])} >= {minimum_runs}",
            test,
        )
        add(
            "candidate_minimum_runs_per_test",
            c["runs"] >= minimum_runs,
            f"{int(c['runs'])} >= {minimum_runs}",
            test,
        )
        add(
            "baseline_success_rate",
            b["success_rate"] >= gates["success_rate_min"],
            f"{b['success_rate']:.6f} >= {gates['success_rate_min']:.6f}",
            test,
        )
        add(
            "candidate_success_rate",
            c["success_rate"] >= gates["success_rate_min"],
            f"{c['success_rate']:.6f} >= {gates['success_rate_min']:.6f}",
            test,
        )
        med = pct_regression(c["median_latency_ns"], b["median_latency_ns"])
        p99 = pct_regression(c["p99_latency_ns"], b["p99_latency_ns"])
        mem = pct_regression(c["peak_memory_bytes"], b["peak_memory_bytes"])
        add(
            "median_latency_regression",
            med <= gates["median_latency_regression_pct_max"],
            f"{med:.3f}% <= {gates['median_latency_regression_pct_max']:.3f}%",
            test,
        )
        add(
            "p99_latency_regression",
            p99 <= gates["p99_latency_regression_pct_max"],
            f"{p99:.3f}% <= {gates['p99_latency_regression_pct_max']:.3f}%",
            test,
        )
        add(
            "peak_memory_regression",
            mem <= gates["peak_memory_regression_pct_max"],
            f"{mem:.3f}% <= {gates['peak_memory_regression_pct_max']:.3f}%",
            test,
        )
        if test in zero_copy:
            add(
                "baseline_zero_copy_payload_bytes_copied",
                b["bytes_copied_max"] <= gates["zero_copy_payload_bytes_copied_max"],
                f"{int(b['bytes_copied_max'])} <= {gates['zero_copy_payload_bytes_copied_max']}",
                test,
            )
            add(
                "candidate_zero_copy_payload_bytes_copied",
                c["bytes_copied_max"] <= gates["zero_copy_payload_bytes_copied_max"],
                f"{int(c['bytes_copied_max'])} <= {gates['zero_copy_payload_bytes_copied_max']}",
                test,
            )

    for label, values in (("baseline", baseline_totals), ("candidate", candidate_totals)):
        add(
            f"{label}_isolation_violations",
            values["isolation_violations"] <= gates["isolation_violations_max"],
            f"{values['isolation_violations']} <= {gates['isolation_violations_max']}",
        )
        add(
            f"{label}_unrecovered_faults",
            values["unrecovered_faults"] <= gates["unrecovered_faults_max"],
            f"{values['unrecovered_faults']} <= {gates['unrecovered_faults_max']}",
        )
        add(
            f"{label}_telemetry_drops",
            values["telemetry_drops"] <= gates["telemetry_drops_max"],
            f"{values['telemetry_drops']} <= {gates['telemetry_drops_max']}",
        )

    passed = all(bool(check["pass"]) for check in checks)
    return {
        "phase": 16,
        "status": "PASS" if passed else "FAIL",
        "baseline_by_test": baseline,
        "candidate_by_test": candidate,
        "baseline_totals": baseline_totals,
        "candidate_totals": candidate_totals,
        "checks": checks,
    }


def run_cli() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument(
        "--gates",
        default=Path(__file__).resolve().parents[1] / "config" / "phase16_gates.json",
        type=Path,
    )
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()

    try:
        baseline_rows = load_csv(args.baseline)
        candidate_rows = load_csv(args.candidate)
        cfg = load_config(args.gates)
        report = evaluate(baseline_rows, candidate_rows, cfg)
        print(f"Nexora Phase 16 release gates: {report['status']}")
        for check in report["checks"]:
            scope = f" [{check['test']}]" if "test" in check else ""
            state = "PASS" if check["pass"] else "FAIL"
            print(f"  {state}  {check['name']}{scope}: {check['detail']}")
        if args.json_out:
            args.json_out.parent.mkdir(parents=True, exist_ok=True)
            args.json_out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        return 0 if report["status"] == "PASS" else 2
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as exc:
        print(f"phase16 validation input error: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(run_cli())
