#!/usr/bin/env python3
"""qa_phase2_4g1_gate.py - Phase 2.4g.1 InvisibleBlock-on-RSDK-engine gate.

BINDING per CLAUDE.md §4.7 + memory/qa-iterative-improvement.md +
memory/ghz-pivot-to-rsdk-engine.md. This gate is authored BEFORE any
2.4g.1 implementation lands and MUST fire RED on the current build.

2.4g.1 is the first increment of the GHZ -> RSDK-engine pivot
(plan: C:\\Users\\gary\\.claude\\plans\\mighty-weaving-bunny.md). It lands
InvisibleBlock (the only Phase-2.4g object that ports cleanly today) as a
REAL RSDK object: registered via rsdk_object_register_ex, spawned from
GHZ Scene1.bin's object table into RSDK slots through the full
rsdk_load_scene path (user decision 2026-05-28: "Full rsdk_load_scene
(literal plan)"), ticked by rsdk_object_tick, and its Update writes
player_t.collisionFlagV/H through the existing Player_CheckCollisionBox.

Decomp authority:
  - tools/_decomp_raw/SonicMania_Objects_Global_InvisibleBlock.c
      _Update: foreach_active(Player) -> planeFilter+noChibi gate ->
        switch Player_CheckCollisionBox: C_TOP->collisionFlagV|=1,
        C_LEFT->collisionFlagH|=1, C_RIGHT->collisionFlagH|=2,
        C_BOTTOM->collisionFlagV|=2 (each gated by !noCrush).
      _Create: hitbox.right = 8*width+8, left = -right,
        bottom = 8*height+8, top = -bottom; active =
        activeNormal ? ACTIVE_NORMAL : ACTIVE_BOUNDS.
  - rsdkv5-src/RSDKv5/RSDK/Scene/Scene.cpp:528-665 LoadSceneAssets ->
    per-class Create dispatch (the full rsdk_load_scene path).

Predicates (static unless noted):

  P1 - InvisibleBlock is a registered RSDK object.
       (a) game.map contains InvisibleBlock_Create AND InvisibleBlock_Update.
       (b) Game.c registers it via rsdk_object_register_ex("InvisibleBlock", ...).

  P2 - player_t carries the decomp-parity collision fields.
       Player.h declares collisionFlagH, collisionFlagV, collisionPlane
       inside the player_t struct (currently absent -> RED).

  P3 - The full-rsdk_load_scene path realities are in place:
       (a) cd/GHZSCN1.BIN exists AND its bytes equal the extracted
           GHZ Scene1.bin (the build pipeline shipped the 85 KB scene).
       (b) scene.c has a sequential-compaction fallback so the 18
           sparse-slotted InvisibleBlocks (scene slots well past the
           80-slot scene window) are recovered into the 32-slot temp
           region instead of being silently dropped (token: "compact").
       (c) a slot-table-clear helper (rsdk_object_clear_scene_slots)
           exists in game.map so stale Title entities don't tick/draw
           in GHZ.

  P4 - (runtime, --with-savestate) >= 1 InvisibleBlock RSDK slot is
       populated for GHZ and the player's collisionFlagV becomes
       nonzero when overlapping a block. Verified by peeking the diag
       globals g_ghz_invblock_spawned (>=1) and g_ghz_invblock_collv
       (nonzero) resolved from game.map. SKIPPED without --with-savestate
       (the static P1-P3 contract carries the RED-now demonstration).

Exit code: 0 = all GREEN. Non-zero = any RED.

Run:
    py -3 tools/qa_phase2_4g1_gate.py
    py -3 tools/qa_phase2_4g1_gate.py --with-savestate samples/qa_phase2_4g1.mcs
"""

import argparse
import hashlib
import os
import re
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE_PATH = os.path.join(ROOT, "extracted", "Data", "Stages", "GHZ", "Scene1.bin")


def cprint(tag, msg, ok):
    colour = "\033[32m" if ok else "\033[31m"
    reset = "\033[0m"
    print(f"  [{colour}{tag}{reset}] {msg}")
    return ok


# --- GHZ Scene1.bin class-entity extraction -------------------------------
# Generalised from qa_phase2_4f_canonical_spawn_gate._parse_player_entity:
# returns the list of (slot, x_px, y_px) for the named class.

def _parse_class_entities(scene_path, class_name):
    if not os.path.exists(scene_path):
        return None
    with open(scene_path, "rb") as f:
        d = f.read()

    class R:
        def __init__(self, b):
            self.d, self.p = b, 0
        def u8(self):
            v = self.d[self.p]; self.p += 1; return v
        def u16(self):
            v = struct.unpack_from("<H", self.d, self.p)[0]; self.p += 2; return v
        def u32(self):
            v = struct.unpack_from("<I", self.d, self.p)[0]; self.p += 4; return v
        def i32(self):
            v = struct.unpack_from("<i", self.d, self.p)[0]; self.p += 4; return v
        def i8(self):
            v = struct.unpack_from("<b", self.d, self.p)[0]; self.p += 1; return v
        def i16(self):
            v = struct.unpack_from("<h", self.d, self.p)[0]; self.p += 2; return v
        def take(self, n):
            v = self.d[self.p:self.p+n]; self.p += n; return v
        def s(self):
            n = self.u8(); v = self.d[self.p:self.p+n]; self.p += n; return v
        def compressed(self):
            total = self.u32()
            _u = struct.unpack_from(">I", self.d, self.p)[0]; self.p += 4
            clen = total - 4
            z = self.d[self.p:self.p+clen]; self.p += clen
            return zlib.decompress(z)

    r = R(d)
    if d[:4] != b"SCN\x00":
        return None
    r.p = 4
    r.take(0x10)
    nl = r.u8()
    r.take(nl + 1)
    layer_count = r.u8()
    for _ in range(layer_count):
        r.u8()
        r.s()
        r.u8()
        r.u8()
        r.u16(); r.u16(); r.u16(); r.u16()
        sic = r.u16()
        for _ in range(sic):
            r.take(6)
        r.compressed()
        r.compressed()

    want_hash = hashlib.md5(class_name.encode("ascii")).digest()
    out = []
    obj_count = r.u8()
    for _ in range(obj_count):
        nhash = r.take(16)
        var_count = r.u8()
        attribs = []
        for _ in range(max(0, var_count - 1)):
            r.take(16)
            attribs.append(r.u8())
        entity_count = r.u16()
        for _ in range(entity_count):
            slot = r.u16()
            x = r.i32()
            y = r.i32()
            for atype in attribs:
                if atype == 0:   r.u8()
                elif atype == 1: r.u16()
                elif atype == 2: r.u32()
                elif atype == 3: r.i8()
                elif atype == 4: r.i16()
                elif atype == 5: r.i32()
                elif atype == 6: r.u32()
                elif atype == 7: r.u32()
                elif atype == 8:
                    n = r.u16(); r.take(n * 2)
                elif atype == 9:
                    r.i32(); r.i32()
                elif atype == 11: r.u32()
                else:
                    return None
            if nhash == want_hash:
                out.append((slot, x >> 16, y >> 16))
    return out


def _read(path):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        return f.read()


# --- P1: InvisibleBlock registered as an RSDK object ----------------------

def predicate_1_registered():
    mp = os.path.join(ROOT, "game.map")
    if not os.path.exists(mp):
        return cprint("P1 RED", "game.map not found (build first)", False)
    body = _read(mp)
    have_create = re.search(r"\bInvisibleBlock_Create\b", body) is not None
    have_update = re.search(r"\bInvisibleBlock_Update\b", body) is not None

    gc = os.path.join(ROOT, "src", "mania", "Game.c")
    reg_ok = False
    if os.path.exists(gc):
        gcb = _read(gc)
        reg_ok = re.search(
            r'rsdk_object_register_ex\s*\(\s*"InvisibleBlock"', gcb) is not None

    if have_create and have_update and reg_ok:
        return cprint("P1 GREEN",
                      "InvisibleBlock_Create+_Update in game.map; registered in Game.c",
                      True)
    return cprint(
        "P1 RED",
        f"create={have_create} update={have_update} register_ex={reg_ok} "
        "(InvisibleBlock not yet a registered RSDK object)",
        False)


# --- P2: player_t carries decomp-parity collision fields ------------------

def predicate_2_player_fields():
    ph = os.path.join(ROOT, "src", "mania", "Objects", "Global", "Player.h")
    if not os.path.exists(ph):
        return cprint("P2 RED", "Player.h not found", False)
    body = _read(ph)
    # Scope to the player_t struct body.
    m = re.search(r"\}\s*player_t\s*;", body)
    struct_body = body[:m.start()] if m else body
    fields = ["collisionFlagH", "collisionFlagV", "collisionPlane"]
    present = {f: (re.search(r"\b" + f + r"\b", struct_body) is not None)
               for f in fields}
    if all(present.values()):
        return cprint("P2 GREEN",
                      "player_t has collisionFlagH+collisionFlagV+collisionPlane",
                      True)
    missing = [f for f, ok in present.items() if not ok]
    return cprint("P2 RED",
                  f"player_t missing decomp-parity fields: {', '.join(missing)}",
                  False)


# --- P3: full-rsdk_load_scene path realities ------------------------------

def predicate_3_scene_path():
    sub = []

    # (a) cd/GHZSCN1.BIN exists and equals extracted GHZ Scene1.bin.
    ghzscn = os.path.join(ROOT, "cd", "GHZSCN1.BIN")
    if os.path.exists(ghzscn) and os.path.exists(SCENE_PATH):
        with open(ghzscn, "rb") as f:
            a = f.read()
        with open(SCENE_PATH, "rb") as f:
            b = f.read()
        a_ok = (a == b)
        sub.append(("3a", a_ok,
                    f"cd/GHZSCN1.BIN={'matches' if a_ok else 'DIFFERS from'} "
                    f"extracted Scene1.bin ({len(a)} vs {len(b)} B)"))
    else:
        sub.append(("3a", False,
                    f"cd/GHZSCN1.BIN exists={os.path.exists(ghzscn)} "
                    "(build pipeline must ship the 85 KB GHZ scene)"))

    # (b) scene.c sequential-compaction fallback recovers sparse slots.
    sc = os.path.join(ROOT, "src", "rsdk", "scene.c")
    scb = _read(sc) if os.path.exists(sc) else ""
    # The fallback must REPLACE the "silently drop" continue for off>=
    # TEMPENTITY_COUNT with a sequential compaction allocator. We require
    # an explicit token so the implementation declares intent.
    compact_ok = re.search(r"compact", scb, re.IGNORECASE) is not None
    sub.append(("3b", compact_ok,
                f"scene.c sequential-compaction fallback "
                f"{'present' if compact_ok else 'ABSENT (18 sparse InvisibleBlocks would be dropped)'}"))

    # (c) slot-table-clear helper exists (stale Title entities must not
    #     tick/draw in GHZ). Symbol present in game.map.
    mp = os.path.join(ROOT, "game.map")
    clear_ok = False
    if os.path.exists(mp):
        clear_ok = re.search(r"\brsdk_object_clear_scene_slots\b",
                             _read(mp)) is not None
    sub.append(("3c", clear_ok,
                f"rsdk_object_clear_scene_slots "
                f"{'in game.map' if clear_ok else 'ABSENT (stale Title slots would persist)'}"))

    all_ok = all(ok for _, ok, _ in sub)
    for tag, ok, msg in sub:
        cprint(f"P3.{tag} {'GREEN' if ok else 'RED'}", msg, ok)
    return all_ok


# --- P4: runtime savestate (optional) -------------------------------------

def _peek32_wram(sections, addr):
    try:
        sys.path.insert(0, os.path.dirname(__file__))
        from mcs_extract import _peek_bytes  # type: ignore
    except Exception:
        return None
    b = _peek_bytes(sections, addr, 4)
    if not b:
        return None
    swapped = bytes([b[1], b[0], b[3], b[2]])
    return int.from_bytes(swapped, "big")


def _resolve_sym(map_body, name):
    m = re.search(r"(0x0[06][0-9a-fA-F]+)\s+" + re.escape(name) + r"\b", map_body)
    return int(m.group(1), 16) if m else None


def predicate_4_runtime(state_path):
    if not state_path:
        return cprint("P4 SKIP",
                      "no --with-savestate; static P1-P3 carry the RED-now contract",
                      True)
    mp = os.path.join(ROOT, "game.map")
    if not os.path.exists(mp):
        return cprint("P4 RED", "game.map not found", False)
    map_body = _read(mp)
    spawned_addr = _resolve_sym(map_body, "g_ghz_invblock_spawned")
    collv_addr = _resolve_sym(map_body, "g_ghz_invblock_collv")
    if spawned_addr is None or collv_addr is None:
        return cprint("P4 RED",
                      f"diag symbols missing (spawned={spawned_addr is not None} "
                      f"collv={collv_addr is not None})",
                      False)
    try:
        sys.path.insert(0, os.path.dirname(__file__))
        from mcs_extract import parse_savestate  # type: ignore
        from pathlib import Path
        sections = parse_savestate(Path(state_path))
    except Exception as e:
        return cprint("P4 RED", f"savestate parse failed: {e}", False)
    spawned = _peek32_wram(sections, spawned_addr)
    collv = _peek32_wram(sections, collv_addr)
    ok = (spawned is not None and spawned >= 1 and
          collv is not None and collv != 0)
    return cprint("P4 GREEN" if ok else "P4 RED",
                  f"InvisibleBlock spawned={spawned} player.collisionFlagV={collv}",
                  ok)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--with-savestate", default="",
                    help="savestate (.mcs/.mc0) captured with the player over an InvisibleBlock")
    args = ap.parse_args()

    print("=== Phase 2.4g.1 InvisibleBlock-on-RSDK-engine gate ===")
    print(f"  GHZ Scene1.bin: {SCENE_PATH}")
    ibs = _parse_class_entities(SCENE_PATH, "InvisibleBlock")
    if ibs is None:
        print("  InvisibleBlock entities: SCENE PARSE FAILED")
    else:
        slots = sorted(s for s, _, _ in ibs)
        print(f"  InvisibleBlock entities in GHZ Scene1.bin: {len(ibs)} "
              f"(slots {slots[0] if slots else '-'}..{slots[-1] if slots else '-'})")

    ok = True
    ok &= predicate_1_registered()
    ok &= predicate_2_player_fields()
    ok &= predicate_3_scene_path()
    ok &= predicate_4_runtime(args.with_savestate.strip())

    if ok:
        print("=== Gate Phase 2.4g.1: GREEN ===")
        return 0
    print("=== Gate Phase 2.4g.1: RED ===")
    return 1


if __name__ == "__main__":
    sys.exit(main())
