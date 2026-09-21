#!/usr/bin/env python3
import argparse, json, os, platform, shutil, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "bench" / "bin"

REQUIRED = {"schema","benchmark","platform","variant","params","metrics"}

def validate_record(obj):
    missing = REQUIRED - obj.keys()
    if missing:
        raise ValueError(f"missing fields: {sorted(missing)}")
    if obj["schema"] != "nexora.bench.v1":
        raise ValueError(f"unsupported schema: {obj['schema']}")
    if not isinstance(obj["params"], dict) or not isinstance(obj["metrics"], dict):
        raise ValueError("params and metrics must be objects")
    return obj

def system_metadata():
    data = {
        "hostname": platform.node(),
        "kernel": platform.release(),
        "system": platform.system(),
        "machine": platform.machine(),
        "python": platform.python_version(),
        "cpu_count": os.cpu_count(),
    }
    try:
        text = Path("/proc/cpuinfo").read_text(errors="replace")
        for line in text.splitlines():
            if line.lower().startswith("model name"):
                data["cpu_model"] = line.split(":",1)[1].strip(); break
    except OSError:
        pass
    try:
        data["process_affinity"] = sorted(os.sched_getaffinity(0))
    except (AttributeError, OSError):
        pass
    try:
        data["cpu_governor"] = Path("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor").read_text().strip()
    except OSError:
        pass
    return data

def run_cmd(cmd, cpu=None):
    preexec = None
    if cpu is not None:
        if not hasattr(os, "sched_setaffinity"):
            raise RuntimeError("CPU affinity requested but os.sched_setaffinity is unavailable")
        def pin():
            try:
                os.sched_setaffinity(0, {cpu})
            except OSError:
                os._exit(126)
        preexec = pin
    p = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, check=False, preexec_fn=preexec)
    if p.returncode != 0:
        raise RuntimeError(f"command failed ({p.returncode}): {' '.join(map(str,cmd))}\nstdout:\n{p.stdout}\nstderr:\n{p.stderr}")
    out = []
    for line in p.stdout.splitlines():
        line=line.strip()
        if not line: continue
        out.append(validate_record(json.loads(line)))
    if not out:
        raise RuntimeError(f"no benchmark JSON produced: {' '.join(map(str,cmd))}")
    return out

def ensure_build():
    p = subprocess.run(["make","-C",str(ROOT / "bench"),"all"], text=True)
    if p.returncode:
        raise SystemExit(p.returncode)

def command_matrix(quick=False):
    if quick:
        return [
            [BIN/"alloc_latency","--mode","malloc","--iterations","500","--warmup","50","--size","4096"],
            [BIN/"alloc_latency","--mode","mmap","--iterations","500","--warmup","50","--size","4096"],
            [BIN/"graph_sched","--variant","scan","--nodes","511","--rounds","5"],
            [BIN/"graph_sched","--variant","ready_queue","--nodes","511","--rounds","5"],
            [BIN/"lifetime_peak","--variant","retain_all","--tensors","2000","--max-lifetime","64"],
            [BIN/"lifetime_peak","--variant","lifetime_aware","--tensors","2000","--max-lifetime","64"],
            [BIN/"zero_copy_ipc","--variant","socket_copy","--iterations","50","--warmup","5","--size","4096"],
            [BIN/"zero_copy_ipc","--variant","shared_mem","--iterations","50","--warmup","5","--size","4096"],
            [BIN/"deadline_tail","--policy","fifo","--jobs","500"],
            [BIN/"deadline_tail","--policy","edf","--jobs","500"],
        ]
    return [
        [BIN/"alloc_latency","--mode","malloc","--iterations","10000","--warmup","1000","--size","4096"],
        [BIN/"alloc_latency","--mode","mmap","--iterations","10000","--warmup","1000","--size","4096"],
        [BIN/"alloc_latency","--mode","malloc","--iterations","5000","--warmup","500","--size","1048576"],
        [BIN/"alloc_latency","--mode","mmap","--iterations","5000","--warmup","500","--size","1048576"],
        [BIN/"graph_sched","--variant","scan","--nodes","2047","--rounds","15"],
        [BIN/"graph_sched","--variant","ready_queue","--nodes","2047","--rounds","15"],
        [BIN/"lifetime_peak","--variant","retain_all","--tensors","20000","--max-lifetime","128"],
        [BIN/"lifetime_peak","--variant","lifetime_aware","--tensors","20000","--max-lifetime","128"],
        [BIN/"zero_copy_ipc","--variant","socket_copy","--iterations","500","--warmup","50","--size","4096"],
        [BIN/"zero_copy_ipc","--variant","shared_mem","--iterations","500","--warmup","50","--size","4096"],
        [BIN/"zero_copy_ipc","--variant","socket_copy","--iterations","250","--warmup","25","--size","262144"],
        [BIN/"zero_copy_ipc","--variant","shared_mem","--iterations","250","--warmup","25","--size","262144"],
        [BIN/"deadline_tail","--policy","fifo","--jobs","5000"],
        [BIN/"deadline_tail","--policy","edf","--jobs","5000"],
    ]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--repetitions",type=int,default=10)
    ap.add_argument("--output",default="results/run")
    ap.add_argument("--cpu",type=int)
    ap.add_argument("--quick",action="store_true")
    args=ap.parse_args()
    if args.repetitions < 1: ap.error("--repetitions must be >= 1")
    ensure_build()
    outdir=(ROOT / args.output) if not Path(args.output).is_absolute() else Path(args.output)
    outdir.mkdir(parents=True, exist_ok=True)
    meta=system_metadata()
    meta.update({"requested_cpu":args.cpu,"quick":args.quick,"repetitions":args.repetitions,"started_unix":time.time()})
    (outdir/"system.json").write_text(json.dumps(meta,indent=2,sort_keys=True)+"\n")
    raw=outdir/"raw.jsonl"
    matrix=command_matrix(args.quick)
    with raw.open("w") as fh:
        for rep in range(args.repetitions):
            for cmd in matrix:
                records=run_cmd([str(x) for x in cmd], cpu=args.cpu)
                for rec in records:
                    rec["run"]={"repetition":rep,"captured_unix":time.time()}
                    rec["host"]={k:v for k,v in meta.items() if k not in {"started_unix","repetitions","quick"}}
                    fh.write(json.dumps(rec,sort_keys=True,separators=(",",":"))+"\n")
                    fh.flush()
    print(raw)

if __name__=="__main__": main()
