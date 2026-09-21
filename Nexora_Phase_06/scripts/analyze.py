#!/usr/bin/env python3
import argparse, csv, json, math, statistics
from collections import defaultdict
from pathlib import Path

REQUIRED={"schema","benchmark","platform","variant","params","metrics"}

def canonical_params(p): return json.dumps(p,sort_keys=True,separators=(",",":"))

def validate_record(obj, where="record"):
    if obj.get("fixture") is True:
        raise ValueError(f"{where}: fixture telemetry is for parser tests only and cannot be analyzed as benchmark evidence")
    missing=REQUIRED-obj.keys()
    if missing: raise ValueError(f"{where}: missing {sorted(missing)}")
    if obj["schema"]!="nexora.bench.v1": raise ValueError(f"{where}: wrong schema")
    if not isinstance(obj["benchmark"],str) or not obj["benchmark"]: raise ValueError(f"{where}: benchmark must be non-empty string")
    if not isinstance(obj["platform"],str) or not obj["platform"]: raise ValueError(f"{where}: platform must be non-empty string")
    if not isinstance(obj["variant"],str) or not obj["variant"]: raise ValueError(f"{where}: variant must be non-empty string")
    if not isinstance(obj["params"],dict): raise ValueError(f"{where}: params must be object")
    if not isinstance(obj["metrics"],dict): raise ValueError(f"{where}: metrics must be object")
    for name,val in obj["metrics"].items():
        if isinstance(val,bool) or not isinstance(val,(int,float)) or not math.isfinite(float(val)):
            raise ValueError(f"{where}: metric {name!r} must be a finite number")
    return obj

def load(paths):
    records=[]
    for path in paths:
        with open(path) as fh:
            for no,line in enumerate(fh,1):
                line=line.strip()
                if not line: continue
                records.append(validate_record(json.loads(line),f"{path}:{no}"))
    return records

def q(vals,p):
    xs=sorted(vals)
    if not xs:return math.nan
    pos=(len(xs)-1)*p
    lo=int(math.floor(pos)); hi=int(math.ceil(pos))
    if lo==hi:return xs[lo]
    return xs[lo]+(xs[hi]-xs[lo])*(pos-lo)

def aggregate(records):
    groups=defaultdict(lambda:defaultdict(list))
    params={}
    for r in records:
        validate_record(r)
        key=(r["benchmark"],r["platform"],r["variant"],canonical_params(r["params"]))
        params[key]=r["params"]
        for name,val in r["metrics"].items(): groups[key][name].append(float(val))
    rows=[]
    for key,metrics in sorted(groups.items()):
        b,pf,v,_=key
        for metric,vals in sorted(metrics.items()):
            rows.append({
                "benchmark":b,"platform":pf,"variant":v,"params":params[key],"metric":metric,
                "n":len(vals),"mean":statistics.fmean(vals),"median":statistics.median(vals),
                "stdev":statistics.stdev(vals) if len(vals)>1 else 0.0,"min":min(vals),"max":max(vals),
                "p95_across_runs":q(vals,.95),
            })
    return rows

def comparability_audit(records):
    groups=defaultdict(lambda:defaultdict(set))
    for r in records:
        key=(r["benchmark"],canonical_params(r["params"]))
        groups[key][r["platform"]].add(r["variant"])
    matched=[]; unmatched=[]
    all_platforms=sorted({r["platform"] for r in records})
    for (benchmark,pjson), platforms in sorted(groups.items()):
        entry={"benchmark":benchmark,"params":json.loads(pjson),"platforms":{k:sorted(v) for k,v in sorted(platforms.items())}}
        if len(platforms)>=2: matched.append(entry)
        else: unmatched.append(entry)
    return {"platforms":all_platforms,"matched_parameter_groups":matched,"unmatched_parameter_groups":unmatched}

def write_outputs(rows,outdir,records=None):
    outdir.mkdir(parents=True,exist_ok=True)
    (outdir/"summary.json").write_text(json.dumps(rows,indent=2,sort_keys=True)+"\n")
    fields=["benchmark","platform","variant","params","metric","n","mean","median","stdev","min","max","p95_across_runs"]
    with (outdir/"summary.csv").open("w",newline="") as fh:
        w=csv.DictWriter(fh,fieldnames=fields); w.writeheader()
        for row in rows:
            rr=dict(row); rr["params"]=canonical_params(rr["params"]); w.writerow(rr)
    if records is None:
        records=[]
        # Reconstruct enough information for platform-only reporting used by unit callers.
        for r in rows:
            records.append({"schema":"nexora.bench.v1","benchmark":r["benchmark"],"platform":r["platform"],"variant":r["variant"],"params":r["params"],"metrics":{r["metric"]:r["mean"]}})
    audit=comparability_audit(records)
    (outdir/"comparability.json").write_text(json.dumps(audit,indent=2,sort_keys=True)+"\n")
    platforms=audit["platforms"]
    lines=["# Nexora Phase 6 Benchmark Report","",f"Platforms present: {', '.join(platforms) or 'none'}.",""]
    if "nexora" not in platforms:
        lines += ["> No Nexora kernel telemetry is present. These data are Linux/reference results only; no Nexora-vs-Linux performance conclusion is justified yet.",""]
    elif "linux" in platforms and not audit["matched_parameter_groups"]:
        lines += ["> Linux and Nexora records are both present, but none have identical benchmark IDs and parameter objects. No cross-platform performance comparison is valid for this dataset.",""]
    lines += ["## Comparability audit",""]
    if audit["matched_parameter_groups"]:
        lines += ["Matched parameter groups present across at least two platforms:",""]
        for e in audit["matched_parameter_groups"]:
            pv="; ".join(f"{pf}: {', '.join(vs)}" for pf,vs in e["platforms"].items())
            lines.append(f"- `{e['benchmark']}` params `{canonical_params(e['params'])}` — {pv}")
    else:
        lines += ["No cross-platform matched parameter groups are present."]
    lines += ["","## Aggregated metrics","","| Benchmark | Platform | Variant | Metric | n | Mean | Median | Across-run p95 |","|---|---|---|---|---:|---:|---:|---:|"]
    for r in rows:
        lines.append(f"| {r['benchmark']} | {r['platform']} | {r['variant']} | {r['metric']} | {r['n']} | {r['mean']:.6g} | {r['median']:.6g} | {r['p95_across_runs']:.6g} |")
    lines += ["","## Interpretation rules","","Compare only records with matching benchmark IDs and matching parameter objects. Prefer medians for central tendency and p95/p99 for latency-sensitive claims. Preserve raw JSONL and system metadata with every result. A difference is an observation, not an architectural attribution, until confounders such as CPU affinity, power state, compiler flags, workload size, virtualization/emulation mode, clock calibration, and instrumentation overhead are controlled.",""]
    (outdir/"report.md").write_text("\n".join(lines))

def main():
    ap=argparse.ArgumentParser(); ap.add_argument("inputs",nargs="+"); ap.add_argument("--out-dir",required=True)
    a=ap.parse_args(); records=load(a.inputs); rows=aggregate(records); write_outputs(rows,Path(a.out_dir),records); print(Path(a.out_dir)/"report.md")
if __name__=="__main__": main()
