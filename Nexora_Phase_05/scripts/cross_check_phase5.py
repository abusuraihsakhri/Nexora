#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]

def fail(msg: str) -> None:
    print(f"CROSS-CHECK FAIL: {msg}", file=sys.stderr)
    raise SystemExit(1)

def read(rel: str) -> str:
    path = ROOT / rel
    if not path.exists():
        fail(f"missing file: {rel}")
    return path.read_text(encoding="utf-8")

roadmap = read("reference/ORIGINAL_ROADMAP.md")
abi_h = read("include/nexora/abi.h")
user_h = read("user/libnexora/nexora.h")
dispatch_c = read("kernel/syscall.c")
abi_doc = read("docs/ABI.md")
readme = read("README.md")
tests = read("tests/test_syscall.c")

m = re.search(r"## Milestone 5 — User mode and syscall ABI.*?```text\n(.*?)```", roadmap, re.S)
if not m:
    fail("could not locate Milestone 5 syscall list in original roadmap")
roadmap_calls = [line.strip() for line in m.group(1).splitlines() if line.strip()]

expected = [
    "ai_tensor_create",
    "ai_tensor_map",
    "ai_tensor_release",
    "ai_work_submit",
    "ai_work_wait",
    "ai_cap_delegate",
    "ai_device_query",
]
if roadmap_calls != expected:
    fail(f"roadmap ABI differs from expected milestone list: {roadmap_calls}")

enum_block = re.search(r"enum nexora_syscall_number\s*\{(.*?)\};", abi_h, re.S)
if not enum_block:
    fail("syscall enum missing")
entries = re.findall(r"NEXORA_SYS_([A-Z0-9_]+)\s*=\s*(\d+)", enum_block.group(1))
num_by_symbol = {name: int(num) for name, num in entries}
expected_symbols = {
    "ABI_QUERY": 0,
    "AI_TENSOR_CREATE": 1,
    "AI_TENSOR_MAP": 2,
    "AI_TENSOR_RELEASE": 3,
    "AI_WORK_SUBMIT": 4,
    "AI_WORK_WAIT": 5,
    "AI_CAP_DELEGATE": 6,
    "AI_DEVICE_QUERY": 7,
    "MAX": 8,
}
if num_by_symbol != expected_symbols:
    fail(f"syscall enum mismatch: {num_by_symbol}")

wrappers = set(re.findall(r"nexora_status_t\s+([a-z0-9_]+)\s*\(", user_h))
for name in ["nexora_abi_query", *expected]:
    if name not in wrappers:
        fail(f"userspace wrapper declaration missing: {name}")

dispatch_cases = set(re.findall(r"case\s+NEXORA_SYS_([A-Z0-9_]+)\s*:", dispatch_c))
for symbol in expected_symbols:
    if symbol in {"ABI_QUERY", "MAX"}:
        continue
    if symbol not in dispatch_cases:
        fail(f"dispatcher case missing: {symbol}")
if "number == NEXORA_SYS_ABI_QUERY" not in dispatch_c:
    fail("ABI query dispatch path missing")

doc_rows = re.findall(r"\|\s*(\d+)\s*\|\s*`([^`]+)`", abi_doc)
doc_map = {name: int(num) for num, name in doc_rows}
for num, name in enumerate(["nexora_abi_query", *expected]):
    if doc_map.get(name) != num:
        fail(f"ABI.md number mismatch for {name}: {doc_map.get(name)} vs {num}")

for name in expected:
    if name not in readme:
        fail(f"README missing Milestone-5 syscall: {name}")

count = len(re.findall(r"^static void test_[a-z0-9_]+\(void\)", tests, re.M))
match = re.search(r"Phase 5 host tests: PASS \((\d+) test groups\)", tests)
if not match or int(match.group(1)) != count:
    fail(f"test count banner does not match test functions ({count})")

print("Phase 5 cross-file consistency: PASS")
print(f"Roadmap milestone calls: {len(expected)} / {len(expected)} matched")
print("Header/docs/wrappers/dispatcher syscall numbering: matched")
print(f"Host test groups declared: {count}")
