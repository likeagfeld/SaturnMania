#!/usr/bin/env python3
"""qa_trace.py -- STRUCTURAL game-state extractor (no per-bug witnesses).

Walks the RSDK entity pool GENERICALLY using the decomp EntityBase struct
offsets (authoritative -- the Saturn build compiles the same headers), so ONE
walker dumps every entity + the key globals as a state vector. Live (RetroArch
UDP) or from a Mednafen savestate. In --watch it emits a per-sample timeline
(JSONL) -- the objective record a playthrough is judged against, no pixels and
no hand-authored address per field. This is the substrate for the parity-oracle
diff and the universal invariants (qa_invariants.py).

EntityBase offsets (MEASURED this session, from Object.hpp / EntityPlayer):
  position.x @ +0, position.y @ +4 (Q16.16)   classID @ +54 (u16 = hi of dword+52)
  onGround @ +72   drawGroup @ +79   visible @ +85   onScreen @ +86
  state ptr @ +88   animator @ +104 (Animator: animationID @ +8 -> entity+112)
Entity stride = 556 (Saturn RESERVE-region wide entity).

Usage:
  python tools/qa_trace.py --live [--slots 700] [--watch 0.5] [--out trace.jsonl]
  python tools/qa_trace.py --state STATE.mcs
Exit 0 always (it's an extractor); parse errors -> stderr + exit 2.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import re
import sys
import time
from pathlib import Path

_HERE = Path(__file__).resolve().parent

# Windows consoles default to cp1252; savestate folder strings can contain
# non-decodable bytes (build/savestate map drift -> a garbage folder). Make
# stdout/stderr tolerate them so a print never crashes the extractor -- this is
# an EXTRACTOR, it must always emit, never raise on OUTPUT. Also covers the
# modules that import qa_trace (qa_invariants, qa_ci).
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(errors="backslashreplace")  # type: ignore[union-attr]
    except Exception:
        pass


def _load(mod, fn):
    spec = importlib.util.spec_from_file_location(mod, _HERE / fn)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)  # type: ignore[union-attr]
    return m


mcs_extract = _load("mcs_extract", "mcs_extract.py")
qa_netmem = _load("qa_netmem", "qa_netmem.py")

STRIDE = 556
# DUAL-STRIDE POOL ADDRESSING (2026-07-21 remap-aware fix, parity-audit critic blind-spot):
# the Saturn pool is dual-stride -- reserve/temp regions use ENTITY_WIDE_SIZE (556) but the
# SCENE region uses P6_POOL_NARROW_STRIDE = sizeof(EntityBase) (344). The old flat
# `pool + slot*556` walk MISALIGNED the scene region (556 overshoots each 344B scene entity)
# -> it saw ~6 of 101 live GHZ entities -> ALL of D3 SPAWN / D4 ANIM / D5 VISIBLE were
# unreliable ("ANIM garbage-animID" leads = misaligned reads, NOT port bugs). Mirror
# SaturnEntityAt (Object.hpp:566-572) EXACTLY. Constants from Object.hpp:60-114 (Saturn block).
POOL_RESERVE = 0x40      # RESERVE_ENTITY_COUNT
POOL_SCENE   = 0x440     # SCENEENTITY_COUNT (== p6_pool_scene_phys sphys until the #263 shrink)
POOL_TEMP    = 0x40      # TEMPENTITY_COUNT
POOL_ENTITY_COUNT = POOL_RESERVE + POOL_SCENE + POOL_TEMP  # 1216
POOL_WIDE    = 556       # ENTITY_WIDE_SIZE
POOL_NARROW  = 344       # sizeof(EntityBase) (64*556 + 1088*344 + 64*556 = 445,440, Object.hpp:90)

def entity_addr(base, slot, sphys=POOL_SCENE):
    """Byte address of scene-pool slot -- mirrors SaturnEntityAt (Object.hpp:566-572).
    Remap is IDENTITY in the current build (ps==slot; the #263 shrink table is cart-resident
    and not netmem-readable -- identity is correct until it ships). The wide-scene UISaveSlot
    sub-pool (P6_WIDESCENE_OFF) is menu-only and not handled here (GHZ/AIZ/cutscene have none)."""
    ps = slot
    if ps < POOL_RESERVE:
        return base + ps * POOL_WIDE
    if ps < POOL_RESERVE + sphys:
        return base + POOL_RESERVE * POOL_WIDE + (ps - POOL_RESERVE) * POOL_NARROW
    return (base + POOL_RESERVE * POOL_WIDE + sphys * POOL_NARROW
            + (ps - POOL_RESERVE - sphys) * POOL_WIDE)
OFF_CLASSDW = 52   # dword: group(lo16) | classID(hi16)
OFF_POS_X = 0
OFF_POS_Y = 4
OFF_ONGROUND = 72
OFF_VISIBLE = 85
OFF_ONSCREEN = 86
OFF_STATE = 88
OFF_ANIMID = 112   # animator(+104) + animationID(+8)


def swap32(v):
    return ((v & 0xFF00FF00) >> 8) | ((v & 0x00FF00FF) << 8)


def s32(v):
    return v - 0x100000000 if v is not None and v >= 0x80000000 else v


class Reader:
    def __init__(self, live, state, host, port):
        self.live = live
        self.mem = qa_netmem.RetroMem(host, port, 2.0) if live else None
        self.sections = None if live else mcs_extract.parse_savestate(Path(state))

    def r32(self, addr):
        if self.live:
            try:
                return self.mem.read32_saturn(addr)
            except Exception:
                return None
        raw = mcs_extract._peek_bytes(self.sections, addr, 4)
        return None if raw is None else swap32(int.from_bytes(raw, "big"))

    def r8(self, addr):
        """Byte at addr, big-endian extraction from its aligned dword (SH-2 BE)."""
        dw = self.r32(addr & ~3)
        if dw is None:
            return None
        return (dw >> (8 * (3 - (addr & 3)))) & 0xFF

    def sym(self, map_text, name):
        m = re.search(r"0x([0-9a-fA-F]{16})\s+" + re.escape(name) + r"\s*$", map_text, re.M)
        return int(m.group(1), 16) if m else None


# The RSDK entity pool is allocated at a FIXED WRAM-L address by the engine
# (P6_LW_ENTITYLIST), stable across relinks -- so the trace does NOT depend on
# the objectEntityList symbol matching the savestate's build (map drift makes the
# symbol read garbage across builds). Prefer the symbol when it's a valid WRAM-L
# pointer; else fall back to this measured base.
POOL_BASE_FALLBACK = 0x00243000


def sample(rd: Reader, map_text: str, nslots: int):
    sa = rd.sym(map_text, "RSDK::objectEntityList")
    pool = rd.r32(sa) if sa else None
    anchor_ok = pool is not None and (0x00200000 <= pool < 0x00300000)
    # LIVE SELF-VERIFY (binding, 2026-07-04): a live reader MUST fail LOUD, never
    # return garbage silently. If the objectEntityList anchor is not a valid WRAM-L
    # pool pointer, the core is stuck at boot (4MB cart not applied -> WRAM zeros,
    # frame-frozen, crc32=0) or the SYSTEM_RAM mapping is wrong -> live reads are
    # NOISE. Raise so no one (me) mistakes garbage for a real reading. The
    # POOL_BASE_FALLBACK is a SAVESTATE-only concession (build/relink drift); a live
    # anchor miss is a HARNESS-HEALTH failure, not drift.
    if rd.live and not anchor_ok:
        raise RuntimeError(
            "LIVE HARNESS UNHEALTHY: objectEntityList=0x%X is not a WRAM-L pool pointer "
            "(expect 0x00200000-0x002FFFFF). The emulated core is stuck at boot (4MB cart "
            "not applied? data-only cue not booting?) OR the READ_CORE_RAM mapping is wrong -- "
            "live reads are GARBAGE and must not be trusted. Fix the live boot before "
            "diagnosing. NOTE: a savestate SNAPSHOT is ALSO forbidden for a change-over-time "
            "question (frozen/stuck/progressing) -- that is what live-watch is for." % (pool or 0))
    if not anchor_ok:
        pool = POOL_BASE_FALLBACK
    ents = []
    if pool:
        for slot in range(min(nslots, POOL_ENTITY_COUNT)):
            b = entity_addr(pool, slot)   # dual-stride (was flat pool+slot*556 -> misaligned scene region)
            cdw = rd.r32(b + OFF_CLASSDW)
            if cdw is None:
                break
            cid = cdw & 0xFFFF   # classID @ struct +54 = LOW 16 of the BE dword @ +52
            if cid == 0 or cid > 0x400:
                continue
            px = rd.r32(b + OFF_POS_X)
            py = rd.r32(b + OFF_POS_Y)
            ents.append({
                "slot": slot, "classID": cid,
                "x": s32(px) >> 16 if px is not None else None,
                "y": s32(py) >> 16 if py is not None else None,
                "animID": (rd.r32(b + OFF_ANIMID) or 0) & 0xFFFF,  # animator.animationID u16 @ +112 (BE low)
                "visible": rd.r8(b + OFF_VISIBLE),   # @+85 game-logic-set (self->visible)
                "onScreen": rd.r8(b + OFF_ONSCREEN), # @+86 engine-set (CheckOnScreen) -- may be 0 on Saturn
                "state": rd.r32(b + OFF_STATE),
            })
    # globals
    folder = None
    csf = rd.sym(map_text, "RSDK::currentSceneFolder")
    if csf is not None:
        if rd.live:
            try:
                folder = rd.mem.read_saturn(csf, 16).split(b"\0")[0].decode(errors="replace")
            except Exception:
                folder = None
        else:
            raw = mcs_extract._peek_bytes(rd.sections, csf, 16)
            if raw:
                bb = bytearray(raw)
                for i in range(0, len(bb) - 1, 2):
                    bb[i], bb[i + 1] = bb[i + 1], bb[i]
                folder = bytes(bb).split(b"\0")[0].decode(errors="replace")
    # classID histogram (structural liveness signal)
    hist = {}
    for e in ents:
        hist[e["classID"]] = hist.get(e["classID"], 0) + 1
    return {"folder": folder, "n_entities": len(ents), "classHist": hist, "entities": ents}


def main(argv=None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--live", action="store_true")
    p.add_argument("--state")
    p.add_argument("--map", default=str(_HERE.parent / "game.map"))
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=55355)
    p.add_argument("--slots", type=int, default=700)
    p.add_argument("--watch", type=float, default=0.0)
    p.add_argument("--out", help="write JSONL trace here (watch mode)")
    p.add_argument("--full", action="store_true", help="print every entity (else summary)")
    a = p.parse_args(argv)
    if not a.live and not a.state:
        p.error("pass --live or --state STATE.mcs")

    map_text = Path(a.map).read_text(errors="replace")
    try:
        rd = Reader(a.live, a.state, a.host, a.port)
    except Exception as e:
        sys.stderr.write(f"qa_trace: {e}\n"); return 2

    out = open(a.out, "w") if a.out else None

    def emit(i):
        s = sample(rd, map_text, a.slots)
        s["t"] = i
        if out:
            out.write(json.dumps(s) + "\n"); out.flush()
        top = sorted(s["classHist"].items(), key=lambda kv: -kv[1])[:6]
        print(f"t{i} folder={s['folder']!r} n={s['n_entities']} topClasses={top}")
        if a.full:
            for e in s["entities"][:40]:
                print(f"    slot{e['slot']:4d} cls{e['classID']:3d} pos=({e['x']},{e['y']}) anim={e['animID']} onScr={e['onScreen']}")

    if a.watch > 0:
        try:
            i = 0
            while True:
                emit(i); i += 1; time.sleep(a.watch)
        except KeyboardInterrupt:
            pass
    else:
        emit(0)
    if out:
        out.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
