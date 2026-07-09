#!/usr/bin/env python3
"""qa_ci.py -- one-command, data-driven validation for the Saturn port.

Runs the PROVEN offline substrate over a per-scene savestate corpus and emits a
single consolidated GREEN/RED verdict. No pixels, no visual check, and no
hand-authored witness per bug -- structural reflection + universal invariants +
toolchain-derived budgets. This is Layer 5 (qa_ci) of the automated-validation
architecture (memory: retroarch-live-memory-harness / skill v2.8.0).

Per scene it runs:
  * qa_trace.sample    -> structural state vector (ONE generic entity-pool walk)
  * qa_invariants      -> universal invariants (INV-sane/-players/-camera/-visible)
  * qa_memcheck.sweep  -> memory-availability budgets (ADVISORY on stored states,
                          because runtime witnesses are only authoritative when the
                          savestate build matches the current game.map; relink drift)
Plus ONE build-static check up front: code-wall _end < ceiling, read from the
CURRENT game.map -- valid regardless of savestate age (the #228 boot-trap class).

Corpus = tools/qa_ci_corpus.json (or --corpus). mode drives INV-camera severity:
  gameplay  camera MUST frame the player            -> INV-camera HARD
  cutscene  camera may be scripted away (some beats) -> INV-camera ADVISORY
  ui        title/menu, no player+camera pair        -> INV-sane + budgets only

Usage:
  python tools/qa_ci.py                                  # whole corpus, stored states
  python tools/qa_ci.py --state X.mcs --mode gameplay    # one ad-hoc state
  python tools/qa_ci.py --corpus other.json --slots 700
Exit 0 = all HARD checks GREEN; 1 = a hard violation; 2 = read/setup error.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_ROOT = _HERE.parent


def _load(mod, fn):
    spec = importlib.util.spec_from_file_location(mod, _HERE / fn)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)  # type: ignore[union-attr]
    return m


qa_trace = _load("qa_trace", "qa_trace.py")           # also fixes stdout encoding on import
qa_invariants = _load("qa_invariants", "qa_invariants.py")
qa_memcheck = _load("qa_memcheck", "qa_memcheck.py")


def _resolve(p: str) -> Path:
    q = Path(p)
    return q if q.is_absolute() else (_ROOT / q)


def build_code_wall(map_text: str):
    """Build-static: _end < the WRAM-H code ceiling (#228). Hard, savestate-age
    independent (read from the CURRENT game.map). Returns (ok, detail)."""
    end = qa_memcheck.map_symbol(map_text, "_end")
    if end is None:
        return (True, "_end symbol not in map (skipped)")
    ceil = qa_memcheck.CODE_WALL_CEIL
    headroom = ceil - end
    return (end < ceil,
            f"_end 0x{end:X} vs ceiling 0x{ceil:X} -> {headroom} B ({headroom/1024:.1f} KB) headroom")


def analyze_scene(scene: dict, map_text: str, slots_override):
    """Return a dict with the scene verdict + rows. Never raises (captures read
    errors into an error verdict) so one bad state can't abort the whole CI."""
    name = scene.get("name", "?")
    mode = scene.get("mode", "gameplay")
    slots = slots_override or scene.get("slots", 400)
    # a fresh --capture writes here; else the stored reference state
    state = scene.get("_fresh") or scene.get("state")
    r = {"name": name, "mode": mode, "state": state,
         "note": scene.get("note", ""), "hard_fails": [], "advisories": [],
         "inv_rows": [], "mem_rows": [], "error": None, "summary": ""}
    path = _resolve(state) if state else None
    if not path or not path.exists():
        r["error"] = f"state not found: {state}"
        return r
    try:
        rd = qa_trace.Reader(False, str(path), "127.0.0.1", 55355)
        s = qa_trace.sample(rd, map_text, slots)
    except Exception as e:  # noqa: BLE001 -- extractor must not abort the suite
        r["error"] = f"trace failed: {e}"
        return r
    r["n_entities"] = s["n_entities"]
    r["folder"] = s.get("folder")
    # A hard invariant failure counts toward the exit code ONLY if this state is
    # attributable to the CURRENT build: freshly --captured, or a corpus entry
    # explicitly flagged "binding". A stored reference state can predate a fix,
    # so hard-failing on it would be a lying gate (the meta-QA rule turned on the
    # CI itself). Stored references are analyzed + REPORTED (advisory), not failed.
    fresh = bool(scene.get("_fresh"))
    binding = fresh or bool(scene.get("binding"))
    r["binding"] = binding

    # universal invariants
    cutscene = mode == "cutscene"
    inv = qa_invariants.invariants(s, cutscene=cutscene)
    for iname, ok, detail, advisory in inv:
        r["inv_rows"].append((iname, ok, detail, advisory))
        if not ok:
            if advisory or not binding:
                stale = "" if binding else " (stale reference -- not current build)"
                r["advisories"].append(f"{iname}: {detail}{stale}")
            else:
                r["hard_fails"].append(f"{iname}: {detail}")

    # memory budgets -- ADVISORY on stored states (runtime witnesses may be stale
    # vs the current map). The code-wall is checked once, build-static, above.
    try:
        mrd = qa_memcheck.Reader(False, str(path), "127.0.0.1", 55355)
        mrows, worst = qa_memcheck.sweep(mrd, map_text, 0.90)
        for mr in mrows:
            mname, used, ceil, frac, unit, mstate = mr
            if mname == "code wall _end":
                continue  # handled build-static, once
            r["mem_rows"].append(mr)
            if mstate == "BREACH":
                r["advisories"].append(f"mem {mname}: {used}/{ceil} (stored-state advisory)")
    except Exception as e:  # noqa: BLE001
        r["advisories"].append(f"memcheck skipped: {e}")

    hf = len(r["hard_fails"])
    r["summary"] = "GREEN" if hf == 0 else f"RED ({hf} hard)"
    return r


def capture_scene(scene: dict, cue: str) -> str | None:
    """Capture a FRESH savestate for this scene from the CURRENT build via
    qa_savestate.ps1 (Mednafen boot + F5). Non-destructive: writes <state>.fresh.mcs
    and returns its path (analyze that instead of the stored reference). Returns
    None if the scene has no capture recipe or the capture failed."""
    cap = scene.get("capture")
    name = scene.get("name", "?")
    if not cap:
        print(f"[capture] {name:16s} SKIP (no recipe -> stored state used)")
        return None
    stored = _resolve(scene["state"])
    fresh = stored.with_suffix(".fresh.mcs")
    cmd = ["pwsh", "-NoProfile", "-File", str(_HERE / "qa_savestate.ps1"),
           "-Cue", cue, "-SaveFrame", str(cap.get("saveframe", 24)),
           "-Out", str(fresh)]
    if cap.get("pressstartat"):
        cmd += ["-PressStartAt", str(cap["pressstartat"]),
                "-PressCount", str(cap.get("presscount", 8)),
                "-PressEvery", str(cap.get("pressevery", 1.0))]
    print(f"[capture] {name:16s} SaveFrame={cap.get('saveframe',24)}s -> {fresh.name}")
    try:
        rc = subprocess.run(cmd, cwd=str(_ROOT)).returncode
    except Exception as e:  # noqa: BLE001
        print(f"[capture] {name:16s} FAILED to launch: {e}")
        return None
    if rc != 0 or not fresh.exists():
        print(f"[capture] {name:16s} FAILED (rc={rc}, exists={fresh.exists()})")
        return None
    print(f"[capture] {name:16s} OK ({fresh.stat().st_size} bytes)")
    return str(fresh)


def main(argv=None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--corpus", default=str(_HERE / "qa_ci_corpus.json"))
    p.add_argument("--state", help="analyze a single ad-hoc state instead of the corpus")
    p.add_argument("--mode", default="gameplay", choices=["gameplay", "cutscene", "ui"])
    p.add_argument("--name", default="adhoc")
    p.add_argument("--map", default=str(_ROOT / "game.map"))
    p.add_argument("--slots", type=int, default=0, help="override pool walk depth for all scenes")
    p.add_argument("--capture", action="store_true",
                   help="capture FRESH states from the current build (Mednafen) before analyzing -> BINDING verdict")
    p.add_argument("--only", help="restrict --capture to this scene name (e.g. title)")
    p.add_argument("--cue", default="game.cue", help="disc image for --capture")
    a = p.parse_args(argv)

    map_text = Path(a.map).read_text(errors="replace") if Path(a.map).exists() else ""
    if not map_text:
        sys.stderr.write(f"qa_ci: WARNING game.map not found at {a.map} (symbols/code-wall skipped)\n")

    if a.state:
        scenes = [{"name": a.name, "state": a.state, "mode": a.mode, "slots": a.slots or 400}]
    else:
        try:
            corpus = json.loads(Path(a.corpus).read_text())
            scenes = corpus["scenes"]
        except Exception as e:  # noqa: BLE001
            sys.stderr.write(f"qa_ci: cannot read corpus {a.corpus}: {e}\n")
            return 2

    print("=" * 74)
    print("qa_ci -- data-driven Saturn-port validation (structural, no pixels)")
    print("=" * 74)

    if a.capture:
        print("-- capturing fresh states from the current build (Mednafen F5) --")
        for sc in scenes:
            if a.only and sc.get("name") != a.only:
                continue
            fresh = capture_scene(sc, a.cue)
            if fresh:
                sc["_fresh"] = fresh
        print("-" * 74)

    # build-static code wall (#228), once
    cw_ok, cw_detail = build_code_wall(map_text) if map_text else (True, "(no map)")
    print(f"[build ] code-wall  {'ok  ' if cw_ok else 'FAIL'} {cw_detail}")

    results = [analyze_scene(sc, map_text, a.slots) for sc in scenes]

    print("-" * 74)
    total_hard = 0 if cw_ok else 1
    total_adv = 0
    for r in results:
        if r["error"]:
            print(f"[scene ] {r['name']:16s} ERROR {r['error']}")
            total_hard += 1
            continue
        btag = "bind" if r.get("binding") else "ref "
        print(f"[scene ] {r['name']:16s} {r['summary']:10s} {btag} mode={r['mode']} n={r.get('n_entities')} :: {r['note']}")
        for iname, ok, detail, advisory in r["inv_rows"]:
            tag = "adv " if advisory else ("ok  " if ok else "FAIL")
            print(f"            {iname:13s} {tag} {detail}")
        for adv in r["advisories"]:
            total_adv += 1
        total_hard += len(r["hard_fails"])

    print("-" * 74)
    if total_adv:
        print(f"advisories ({total_adv}) -- reported, non-failing:")
        for r in results:
            for adv in r["advisories"]:
                print(f"   ~ [{r['name']}] {adv}")
    print("=" * 74)
    verdict = "GREEN" if total_hard == 0 else f"RED ({total_hard} hard violation(s))"
    print(f"qa_ci: {len(results)} scene(s), {total_adv} advisory -> {verdict}")
    return 0 if total_hard == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
