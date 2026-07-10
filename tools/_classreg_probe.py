#!/usr/bin/env python3
"""_classreg_probe.py -- LIVE root-cause probe for the first-life missing
class-registration at the chain GHZCutscene->GHZ landing (signpost campaign r2).

Reads, at GHZ entry and periodically after (across death->respawn edges):
  - sceneInfo.classCount (u16 @ sceneInfo+30, Scene.hpp:113-131)
  - stageObjectIDs[0..classCount] (WRAM-L abs 0x002FEF80, p6_io_main.cpp:2971)
  - the referenced ObjectClass entries (backing 0x060D8000, 72 B/entry:
    hash16 + 10 fnptrs + staticVars + entityClassSize + staticClassSize + name;
    REV0U + !ORIGINAL_CODE, Object.hpp:340-384) -> staticVars ptr, *staticVars,
    (*staticVars)->classID (u16 @ +0)
  - name resolution via md5(name) digest match (parse_title_entities.py:55 idiom)
  - dataStorage[DATASET_STG] usedStorage/storageLimit/entryCount/entryCapacity
    (pointer-form @ RSDK::dataStorage sym; DataStorage 32 B, Storage.hpp:42-51)
  - witnesses: p6_w_b2_cids[9], p6_w_brg_classid, p6_scan_loadbuild_seq,
    p6_w_scancull_n/near, p6_w_cont_frames, player classID (death edges)

Run AFTER: pwsh tools/qa_live.ps1 -NoCdda -NoMonitor
"""
import hashlib
import importlib.util
import re
import sys
import time
from pathlib import Path

_HERE = Path(__file__).resolve().parent


def _load(mod, fn):
    spec = importlib.util.spec_from_file_location(mod, _HERE / fn)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


qa_netmem = _load("qa_netmem", "qa_netmem.py")
mem = qa_netmem.RetroMem("127.0.0.1", 55355, 2.0)
MAP = Path(_HERE.parent / "game.map").read_text(errors="replace")


def sym(name):
    m = re.search(r"0x([0-9a-fA-F]{16})\s+" + re.escape(name) + r"\s*$", MAP, re.M)
    return int(m.group(1), 16) if m else None


def r32(a):
    try:
        return mem.read32_saturn(a)
    except Exception:
        return None


def r32s(n):
    a = sym(n)
    return r32(a) if a else None


def r16(a):
    try:
        return mem.read16_saturn(a)
    except Exception:
        return None


def rb(a, n):
    out = bytearray()
    left, cur = n, a
    while left > 0:
        take = min(2000, left)
        out += mem.read_saturn(cur, take)
        cur += take
        left -= take
    return bytes(out)


OBJCLASS_BASE = 0x060D8000
OBJCLASS_SIZE = 72
STAGEIDS = 0x002FEF80
SCENEINFO = sym("RSDK::sceneInfo")
DATASTORAGE_PTR = sym("RSDK::dataStorage")
POOL = None

NAMES = ["Player", "SignPost", "ActClear", "Bridge", "Spring", "PlaneSwitch",
         "SpikeLog", "Spikes", "ItemBox", "Debris", "InvincibleStars",
         "Platform", "InvisibleBlock", "Ring", "Motobug", "Crabmeat",
         "BuzzBomber", "Chopper", "Newtron", "Batbrain", "BadnikHelpers",
         "Explosion", "Animals", "Camera", "Zone", "Music", "StarPost",
         "BoundsMarker", "CollapsingPlatform", "ScoreBonus", "GHZSetup",
         "TitleCard", "ScreenWrap", "ForceSpin", "SpinBooster", "Decoration",
         "DustPuff", "PhantomRuby", "ParallaxSprite"]
H2N = {hashlib.md5(n.encode()).digest(): n for n in NAMES}
# per-4-byte-word-swapped variant (in case the stored uint32[4] is LE words)
def wswap(d):
    return b"".join(d[i:i+4][::-1] for i in range(0, 16, 4))
H2N.update({wswap(hashlib.md5(n.encode()).digest()): n + "(ws)" for n in NAMES})


def folder():
    a = sym("RSDK::currentSceneFolder")
    try:
        return mem.read_saturn(a, 16).split(b"\0")[0].decode(errors="replace")
    except Exception:
        return None


def be16(b, o):
    return (b[o] << 8) | b[o + 1]


def be32(b, o):
    return int.from_bytes(b[o:o+4], "big")


def snapshot(tag):
    print("\n----- SNAPSHOT %s (t=%.1fs) -----" % (tag, time.time() - T0), flush=True)
    si = rb(SCENEINFO, 40)
    class_count = be16(si, 30)
    list_pos = be16(si, 24)
    state = si[37] if len(si) > 37 else None
    print("sceneInfo: classCount=%d listPos=%d folder=%r" % (class_count, list_pos, folder()))
    if class_count == 0 or class_count > 0x100:
        print("  (classCount implausible -- skipping table walk)")
        return
    ids_raw = rb(STAGEIDS, 4 * class_count)
    ids = [be32(ids_raw, 4 * i) for i in range(class_count)]
    # bulk-read the whole objectClassList backing once
    maxid = max(ids)
    cls_raw = rb(OBJCLASS_BASE, OBJCLASS_SIZE * (maxid + 1))
    rows = []
    for o, cid_idx in enumerate(ids):
        off = cid_idx * OBJCLASS_SIZE
        h = cls_raw[off:off+16]
        nm = H2N.get(h, "?")
        sv_ptr = be32(cls_raw, off + 16 + 40)          # staticVars (Object **)
        sv = r32(sv_ptr) if 0x00200000 <= sv_ptr < 0x06100000 or 0x02000000 <= sv_ptr < 0x03000000 else None
        clsid = None
        if sv and (0x00200000 <= sv < 0x00300000 or 0x06000000 <= sv < 0x06100000):
            clsid = r16(sv)
        rows.append((o, cid_idx, nm, sv_ptr, sv, clsid))
    n_null = sum(1 for r in rows if not r[4])
    print("stage classes: %d total, %d with *staticVars==NULL/unreadable" % (len(rows), n_null))
    for r in rows:
        flag = "" if (r[4] and r[5] == r[0]) else "   <<<"
        print("  o=%3d listIdx=%3d %-18s svp=0x%08X sv=%s classID=%s%s" %
              (r[0], r[1], r[2], r[3],
               ("0x%08X" % r[4]) if r[4] else r[4], r[5], flag))
    # DATASET_STG storage state
    ds = r32(DATASTORAGE_PTR)
    if ds:
        d = rb(ds, 32)
        print("DATASET_STG: memtab=0x%08X used=%d(words)=%dB limit=%dB entries=%d/%d clear=%d" %
              (be32(d, 0), be32(d, 4), be32(d, 4) * 4, be32(d, 8),
               be32(d, 24), be32(d, 20), be32(d, 28)))
    # witnesses
    w = {n: r32s(n) for n in ("p6_w_brg_classid", "p6_scan_loadbuild_seq",
                              "p6_w_scancull_n", "p6_w_scancull_near",
                              "p6_w_cont_frames", "p6_w_ovl_bytes",
                              "RSDK::p6_pool_scene_phys")}
    b2a = sym("p6_w_b2_cids")
    b2 = [r32(b2a + 4 * i) for i in range(9)] if b2a else None
    print("witness:", {k.split("::")[-1]: v for k, v in w.items()})
    print("b2_cids:", b2)


T0 = time.time()
# harness health
POOL = r32s("RSDK::objectEntityList")
if POOL is None or not (0x00200000 <= POOL < 0x00300000):
    print("HARNESS UNHEALTHY: objectEntityList=%s" % (hex(POOL) if POOL else POOL))
    sys.exit(2)

WAIT = float(sys.argv[1]) if len(sys.argv) > 1 else 480.0
seen = set()
while time.time() - T0 < WAIT:
    f = folder()
    if f not in seen:
        seen.add(f)
        print("[%6.1fs] folder=%r cont=%s" % (time.time() - T0, f, r32s("p6_w_cont_frames")), flush=True)
    if f == "GHZ":
        break
    time.sleep(0.5)
if folder() != "GHZ":
    print("never reached GHZ (%s)" % sorted(map(str, seen)))
    sys.exit(2)

snapshot("GHZ-LANDING(first life, immediate)")
# then watch across death/respawn edges via player classID
prev_cid = None
next_periodic = time.time() + 6.0
END = time.time() + 120.0
while time.time() < END:
    pc = r16(POOL + 54)
    if pc is None:
        print("!! read failed (RA flake?)")
        break
    if prev_cid is not None and pc != prev_cid and (pc == 0 or prev_cid == 0):
        snapshot("player-cid-edge %s->%s" % (prev_cid, pc))
    prev_cid = pc
    if time.time() > next_periodic:
        next_periodic = time.time() + 15.0
        snapshot("periodic")
    f = folder()
    if f != "GHZ":
        print("scene left GHZ (%r) -- done" % f)
        break
    time.sleep(0.3)
print("probe done.")
