#!/usr/bin/env python3
import argparse,json,math,sys
PREFIX="NEXORA_BENCH "
REQUIRED={"schema","benchmark","platform","variant","params","metrics"}

def validate(obj,where):
    missing=REQUIRED-obj.keys()
    if missing: raise SystemExit(f"{where}: missing {sorted(missing)}")
    if obj["schema"]!="nexora.bench.v1": raise SystemExit(f"{where}: unsupported schema")
    if obj["platform"]!="nexora": raise SystemExit(f"{where}: platform must be nexora")
    if not isinstance(obj["benchmark"],str) or not obj["benchmark"]: raise SystemExit(f"{where}: benchmark must be non-empty string")
    if not isinstance(obj["variant"],str) or not obj["variant"]: raise SystemExit(f"{where}: variant must be non-empty string")
    if not isinstance(obj["params"],dict): raise SystemExit(f"{where}: params must be object")
    if not isinstance(obj["metrics"],dict) or not obj["metrics"]: raise SystemExit(f"{where}: metrics must be non-empty object")
    for name,val in obj["metrics"].items():
        if isinstance(val,bool) or not isinstance(val,(int,float)) or not math.isfinite(float(val)):
            raise SystemExit(f"{where}: metric {name!r} must be a finite number")
    return obj

def main():
    ap=argparse.ArgumentParser(); ap.add_argument("serial_log"); a=ap.parse_args()
    n=0
    with open(a.serial_log,errors="replace") as fh:
        for no,line in enumerate(fh,1):
            pos=line.find(PREFIX)
            if pos<0: continue
            raw=line[pos+len(PREFIX):].strip(); where=f"{a.serial_log}:{no}"
            try: obj=json.loads(raw)
            except json.JSONDecodeError as e: raise SystemExit(f"{where}: invalid benchmark JSON: {e}")
            print(json.dumps(validate(obj,where),sort_keys=True,separators=(",",":"))); n+=1
    if n==0: print("warning: no NEXORA_BENCH records found",file=sys.stderr)
if __name__=="__main__": main()
