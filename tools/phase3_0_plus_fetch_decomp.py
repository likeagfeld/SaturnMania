#!/usr/bin/env python3
"""
phase3_0_plus_fetch_decomp.py - Batch-fetch missing decomp .c/.h files via
GitHub blob API by SHA (computed from a single `git/trees/master?recursive=1`
call).

Reads tools/_phase3plus_fetch_plan.json (list of {cat,cls,kind,sha}). Writes
each blob to tools/_decomp_raw/SonicMania_Objects_<cat>_<cls>.<kind>.

Uses gh api in parallel via concurrent.futures.
"""
import base64
import json
import os
import subprocess
import sys
import concurrent.futures

HERE = os.path.dirname(os.path.abspath(__file__))
RAW = os.path.join(HERE, "_decomp_raw")
PLAN = os.path.join(HERE, "_phase3plus_fetch_plan.json")


def fetch_one(item):
    cat, cls, kind, sha = item["cat"], item["cls"], item["kind"], item["sha"]
    fname = f"SonicMania_Objects_{cat}_{cls}.{kind}"
    dest = os.path.join(RAW, fname)
    if os.path.isfile(dest) and os.path.getsize(dest) > 0:
        return (fname, "cached")
    try:
        r = subprocess.run(
            ["gh", "api", f"repos/RSDKModding/Sonic-Mania-Decompilation/git/blobs/{sha}"],
            capture_output=True, text=True, timeout=60,
        )
        if r.returncode != 0:
            return (fname, f"err: {r.stderr.strip()[:100]}")
        d = json.loads(r.stdout)
        content = base64.b64decode(d["content"])
        with open(dest, "wb") as f:
            f.write(content)
        return (fname, f"OK {len(content)}B")
    except Exception as e:
        return (fname, f"exc: {e}")


def main():
    with open(PLAN) as f:
        plan = json.load(f)
    os.makedirs(RAW, exist_ok=True)
    print(f"Fetching {len(plan)} blobs in parallel (workers=12)...")
    ok = 0
    err = 0
    cached = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=12) as ex:
        for i, (fname, status) in enumerate(ex.map(fetch_one, plan)):
            if status.startswith("OK"):
                ok += 1
            elif status == "cached":
                cached += 1
            else:
                err += 1
                print(f"  FAIL {fname}: {status}", file=sys.stderr)
            if (i + 1) % 50 == 0:
                print(f"  {i+1}/{len(plan)} ok={ok} cached={cached} err={err}")
    print(f"Done: ok={ok} cached={cached} err={err}")


if __name__ == "__main__":
    main()
