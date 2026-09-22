#!/usr/bin/env python3
"""Nexora Phase 15 benchmark parser, summarizer and regression gate."""
from __future__ import annotations

import argparse
import json
import math
import statistics
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

SCHEMA = "nexora.bench.v1"
KEY_FIELDS = ("benchmark", "metric", "unit", "variant", "workload")


def load_json(path: str | Path) -> Any:
    return json.loads(Path(path).read_text(encoding="utf-8"))


def dump_json(obj: Any, path: str | Path | None) -> None:
    text = json.dumps(obj, indent=2, sort_keys=True) + "\n"
    if path:
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        Path(path).write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)


def event_key(event: dict[str, Any]) -> tuple[str, str, str, str, str]:
    return tuple(str(event.get(f, "")) for f in KEY_FIELDS)  # type: ignore[return-value]


def key_string(key: tuple[str, str, str, str, str]) -> str:
    return "|".join(key)


def validate_event(event: Any, line_no: int | None = None) -> dict[str, Any]:
    where = f" line {line_no}" if line_no is not None else ""
    if not isinstance(event, dict):
        raise ValueError(f"event{where} is not a JSON object")
    required = ("schema", "run_id", "benchmark", "metric", "value", "unit", "iteration")
    missing = [k for k in required if k not in event]
    if missing:
        raise ValueError(f"event{where} missing fields: {', '.join(missing)}")
    if event["schema"] != SCHEMA:
        raise ValueError(f"event{where} has unsupported schema {event['schema']!r}")
    if not isinstance(event["value"], (int, float)) or isinstance(event["value"], bool):
        raise ValueError(f"event{where} value is not numeric")
    if not math.isfinite(float(event["value"])):
        raise ValueError(f"event{where} value must be finite")
    if not isinstance(event["iteration"], int) or isinstance(event["iteration"], bool) or event["iteration"] < 0:
        raise ValueError(f"event{where} iteration must be a non-negative integer")
    for field in ("run_id", "benchmark", "metric", "unit"):
        if not isinstance(event[field], str) or not event[field]:
            raise ValueError(f"event{where} field {field!r} must be a non-empty string")
    return event


def read_events(path: str | Path) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    with Path(path).open("r", encoding="utf-8") as fh:
        for i, raw in enumerate(fh, 1):
            line = raw.strip()
            if not line:
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"invalid JSON on line {i}: {exc.msg}") from exc
            events.append(validate_event(event, i))
    if not events:
        raise ValueError("no benchmark events found")
    return events


def quantile(values: list[float], q: float) -> float:
    if not values:
        raise ValueError("quantile of empty sample")
    xs = sorted(values)
    if len(xs) == 1:
        return xs[0]
    pos = (len(xs) - 1) * q
    lo = math.floor(pos)
    hi = math.ceil(pos)
    if lo == hi:
        return xs[lo]
    frac = pos - lo
    return xs[lo] * (1.0 - frac) + xs[hi] * frac


def summarize_events(events: Iterable[dict[str, Any]]) -> dict[str, Any]:
    groups: dict[tuple[str, str, str, str, str], list[float]] = defaultdict(list)
    run_ids: set[str] = set()
    for event in events:
        groups[event_key(event)].append(float(event["value"]))
        run_ids.add(str(event["run_id"]))

    metrics: dict[str, Any] = {}
    for key in sorted(groups):
        vals = groups[key]
        benchmark, metric, unit, variant, workload = key
        metrics[key_string(key)] = {
            "benchmark": benchmark,
            "metric": metric,
            "unit": unit,
            "variant": variant,
            "workload": workload,
            "count": len(vals),
            "min": min(vals),
            "max": max(vals),
            "mean": statistics.fmean(vals),
            "median": statistics.median(vals),
            "p95": quantile(vals, 0.95),
            "p99": quantile(vals, 0.99),
        }
    return {"schema": "nexora.bench.summary.v1", "run_ids": sorted(run_ids), "metrics": metrics}


def match_rule(metric: dict[str, Any], thresholds: dict[str, Any]) -> dict[str, Any]:
    merged = dict(thresholds.get("defaults", {}))
    rules = thresholds.get("rules", [])
    best_score = -1
    for rule in rules:
        fields = ("benchmark", "metric", "unit", "variant", "workload")
        if all(field not in rule or str(rule[field]) == str(metric.get(field, "")) for field in fields):
            score = sum(1 for field in fields if field in rule)
            if score > best_score:
                best_score = score
                merged.update(rule)
    return merged


def compare_summaries(baseline: dict[str, Any], candidate: dict[str, Any], thresholds: dict[str, Any]) -> dict[str, Any]:
    b_metrics = baseline.get("metrics", {})
    c_metrics = candidate.get("metrics", {})
    results: list[dict[str, Any]] = []
    failures = 0

    keys = sorted(set(b_metrics) | set(c_metrics))
    for key in keys:
        b = b_metrics.get(key)
        c = c_metrics.get(key)
        exemplar = c or b or {}
        rule = match_rule(exemplar, thresholds)
        required = bool(rule.get("required", True))
        if b is None or c is None:
            status = "fail" if required else "skip"
            if status == "fail":
                failures += 1
            results.append({"key": key, "status": status, "reason": "missing baseline" if b is None else "missing candidate", "rule": rule})
            continue

        statistic = str(rule.get("statistic", "median"))
        if statistic not in b or statistic not in c:
            failures += 1
            results.append({"key": key, "status": "fail", "reason": f"statistic {statistic!r} unavailable", "rule": rule})
            continue

        min_samples = int(rule.get("min_samples", 1))
        if int(b.get("count", 0)) < min_samples or int(c.get("count", 0)) < min_samples:
            allow = bool(rule.get("allow_insufficient_samples", False))
            status = "warn" if allow else "fail"
            if status == "fail":
                failures += 1
            results.append({
                "key": key,
                "status": status,
                "reason": "insufficient samples",
                "baseline_count": b.get("count", 0),
                "candidate_count": c.get("count", 0),
                "rule": rule,
            })
            continue

        bv = float(b[statistic])
        cv = float(c[statistic])
        direction = str(rule.get("direction", "lower"))
        if bv == 0.0:
            delta_pct = 0.0 if cv == 0.0 else math.inf
        elif direction == "higher":
            delta_pct = (bv - cv) / abs(bv) * 100.0
        else:
            delta_pct = (cv - bv) / abs(bv) * 100.0

        limit = float(rule.get("max_regression_pct", 0.0))
        status = "pass" if delta_pct <= limit else "fail"
        if status == "fail":
            failures += 1
        results.append({
            "key": key,
            "status": status,
            "statistic": statistic,
            "direction": direction,
            "baseline": bv,
            "candidate": cv,
            "regression_pct": delta_pct,
            "max_regression_pct": limit,
            "rule": rule,
        })

    return {
        "schema": "nexora.bench.comparison.v1",
        "status": "pass" if failures == 0 else "fail",
        "failure_count": failures,
        "results": results,
    }


def extract_events(input_path: str | Path, output_path: str | Path) -> int:
    count = 0
    out = Path(output_path)
    out.parent.mkdir(parents=True, exist_ok=True)
    with Path(input_path).open("r", encoding="utf-8", errors="replace") as src, out.open("w", encoding="utf-8") as dst:
        for line_no, raw in enumerate(src, 1):
            start = raw.find("{")
            if start < 0 or SCHEMA not in raw:
                continue
            candidate = raw[start:].strip()
            try:
                event = validate_event(json.loads(candidate), line_no)
            except (json.JSONDecodeError, ValueError):
                continue
            dst.write(json.dumps(event, sort_keys=True, separators=(",", ":")) + "\n")
            count += 1
    return count


def cmd_validate(args: argparse.Namespace) -> int:
    events = read_events(args.input)
    print(f"valid: {len(events)} events")
    return 0


def cmd_summarize(args: argparse.Namespace) -> int:
    summary = summarize_events(read_events(args.input))
    dump_json(summary, args.output)
    return 0


def cmd_compare(args: argparse.Namespace) -> int:
    comparison = compare_summaries(load_json(args.baseline), load_json(args.candidate), load_json(args.thresholds))
    dump_json(comparison, args.output)
    return 0 if comparison["status"] == "pass" else 2


def cmd_extract(args: argparse.Namespace) -> int:
    count = extract_events(args.input, args.output)
    if count == 0:
        print("no benchmark events extracted", file=sys.stderr)
        return 3
    print(f"extracted: {count} events")
    return 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="nxbench", description=__doc__)
    sp = p.add_subparsers(dest="command", required=True)

    v = sp.add_parser("validate", help="validate a benchmark JSONL file")
    v.add_argument("input")
    v.set_defaults(func=cmd_validate)

    s = sp.add_parser("summarize", help="summarize raw benchmark events")
    s.add_argument("input")
    s.add_argument("--output", "-o")
    s.set_defaults(func=cmd_summarize)

    c = sp.add_parser("compare", help="compare candidate summary against baseline")
    c.add_argument("baseline")
    c.add_argument("candidate")
    c.add_argument("--thresholds", required=True)
    c.add_argument("--output", "-o")
    c.set_defaults(func=cmd_compare)

    e = sp.add_parser("extract", help="extract benchmark JSON events from mixed serial logs")
    e.add_argument("input")
    e.add_argument("--output", "-o", required=True)
    e.set_defaults(func=cmd_extract)
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return int(args.func(args))
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"nxbench: error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
