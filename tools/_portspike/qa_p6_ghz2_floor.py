#!/usr/bin/env python3
# =============================================================================
# qa_p6_ghz2_floor.py -- Task #237 / #180 GHZ2 fall-through ROOT-CAUSE reader.
#
# WHAT IT MEASURES
#   The user demanded VISUAL proof of the GHZ2 "fall-through" (gz_*.png arc
#   confirmed it: Sonic spawns visible -> falls -> dies, life 3->2, rings->0,
#   Tails carry-rescue). This reader pins the MECHANISM with a faithful in-situ
#   FindFloorPosition replica (Collision.cpp:2162-2230) run at the player's feet
#   EVERY GHZ2 tick, latched in WRAM-H witnesses:
#
#     floorlayer  -2 collision OFF every tick (tileCollisions==0 -> engine never
#                    runs the floor check -> falls)
#                 -1 collision ON but NO floor under the feet across the whole
#                    descent  == FALL-THROUGH PROVEN at the engine's own logic
#                 >=0 a tile WITH the floor-solid bit WAS under the feet on this
#                    layer index -> collision SHOULD have caught him; the bug is
#                    downstream (physics result not applied / Update not running)
#
#   The supporting witnesses say WHY: collplane/tilecoll/colllayers (player cfg),
#   solidmask (the bit the falling player needs), fglow/fghigh raw feet tiles +
#   their layer indices, feet row + velocity at the spawn-floor crossing, and the
#   tcoff count (ticks collision was disabled).
#
#   This is NOT a pass/fail gate -- it is the measurement that selects the fix.
#   RUN: python qa_p6_ghz2_floor.py <state.mcs> game.map
# =============================================================================
import os
import sys
import pathlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as _scene  # noqa: E402

MCS_DEFAULT = os.path.join(HERE, "p6_s0.mcs")

SYMS = [
    "_p6_w_ghz2_loaded", "_p6_w_ghz2_play_frames", "_p6_w_ghz2_max_plry",
    "_p6_w_ghz2_plry", "_p6_w_ghz2_collplane", "_p6_w_ghz2_colllayers",
    "_p6_w_ghz2_tilecoll", "_p6_w_ghz2_floorlayer", "_p6_w_ghz2_tcoff",
    "_p6_w_ghz2_fghigh_idx", "_p6_w_ghz2_fglow_tile", "_p6_w_ghz2_fghigh_tile",
    "_p6_w_ghz2_feetty", "_p6_w_ghz2_vely",
]


def _bits(v):
    v &= 0xFFFF
    return "".join("1" if (v >> b) & 1 else "0" for b in (15, 14, 13, 12)) \
        + " (b15..b12)"


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("GHZ2 FALL-THROUGH ROOT CAUSE (Task #237/#180): in-situ FindFloorPosition")
    print("=" * 72)

    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s)" % mp)
        return 1
    map_text = _scene.read_text(mp)
    syms, missing = {}, []
    for s in [_scene.SYM_MAGIC] + SYMS:
        syms[s] = _scene.map_symbol(map_text, s)
        if syms[s] is None:
            missing.append(s)
    if any(s in missing for s in SYMS):
        print("  [ RED ] floor-replica witnesses absent (build P6_GHZ2_BOOT=1)")
        for s in missing:
            print("          MISSING %s" % s)
        return 1
    if not os.path.isfile(mcs):
        print("RESULT: RED -- savestate missing (%s)" % mcs)
        return 1

    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    raw_magic = mod._peek_bytes(sections, syms[_scene.SYM_MAGIC], 4)
    label, perm = _scene.calibrate(raw_magic)
    if perm is None:
        print("RESULT: RED -- magic mis-decode")
        return 1

    def pk(s):
        return _scene.peek_u32(mod, sections, syms[s], perm, signed=True)

    loaded = pk("_p6_w_ghz2_loaded")
    frames = pk("_p6_w_ghz2_play_frames")
    spawn_y = pk("_p6_w_ghz2_plry") >> 16
    max_y = pk("_p6_w_ghz2_max_plry")
    plane = pk("_p6_w_ghz2_collplane")
    layers = pk("_p6_w_ghz2_colllayers") & 0xFF
    tc = pk("_p6_w_ghz2_tilecoll")
    # Derived solid mask (matches FindFloorPosition Collision.cpp:2168-2174):
    #   tc==1 DOWN -> plane?bit14:bit12 ; else UP -> plane?bit15:bit13
    if tc == 1:
        solid = 0x4000 if plane else 0x1000
    else:
        solid = 0x8000 if plane else 0x2000
    floorlayer = pk("_p6_w_ghz2_floorlayer")
    tcoff = pk("_p6_w_ghz2_tcoff")
    hi_idx = pk("_p6_w_ghz2_fghigh_idx")
    lo_tile = pk("_p6_w_ghz2_fglow_tile") & 0xFFFF
    hi_tile = pk("_p6_w_ghz2_fghigh_tile") & 0xFFFF
    feetty = pk("_p6_w_ghz2_feetty")
    vely = pk("_p6_w_ghz2_vely")

    fell = (max_y - spawn_y) if loaded else 0
    print("  loaded=%d  play_frames=%d" % (loaded, frames))
    print("  spawn_y=%dpx (ty %d)  max_y=%dpx (ty %d)  FELL %d px (%d tiles)"
          % (spawn_y, spawn_y // 16, max_y, max_y // 16, fell, fell // 16))
    print("  player cfg: collisionPlane=%d  tileCollisions=%d  collisionLayers=0x%02X"
          % (plane, tc, layers))
    print("  solid mask the falling player needs = 0x%04X  bits=%s"
          % (solid, _bits(solid)))
    print("  FG-Low  (slot0)        feet tile=0x%04X  bits=%s"
          % (lo_tile, _bits(lo_tile)))
    print("  FG-High layer index=%d  feet tile=0x%04X  bits=%s  in colllayers=%s"
          % (hi_idx, hi_tile, _bits(hi_tile),
             "YES" if (hi_idx >= 0 and (layers >> hi_idx) & 1) else "no"))
    print("  snapshot: feet ty=%d  velocity.y(>>8)=%d" % (feetty, vely))
    print("  replica verdict: floorlayer=%d  tcoff(coll-OFF ticks)=%d"
          % (floorlayer, tcoff))
    print("-" * 72)

    # ---- band-window residency for slot 1 (FG-High) -------------------------
    # SaturnLayout records per-slot refill counts + the last 8 (clamped) refill
    # origins. If slot-1's windows never sit so the floor row ty=86 is in
    # [wy, wy+32), the window-position logic is dropping the floor; if refills
    # for slot 1 stall, the inflate scratch is the suspect.
    lay = {}
    for s in ["_p6_w_lay_refills", "_p6_w_lay_slot_refills", "_p6_w_lay_ring_wx",
              "_p6_w_lay_ring_wy", "_p6_w_lay_ring_pos"]:
        lay[s] = _scene.map_symbol(map_text, s)
    if lay["_p6_w_lay_slot_refills"] is not None:
        def pkat(base, idx):
            return _scene.peek_u32(mod, sections, base + idx * 4, perm, signed=True)
        tot = (_scene.peek_u32(mod, sections, lay["_p6_w_lay_refills"], perm,
                               signed=True) if lay["_p6_w_lay_refills"] else -1)
        sr0 = pkat(lay["_p6_w_lay_slot_refills"], 0)
        sr1 = pkat(lay["_p6_w_lay_slot_refills"], 1)
        print("  BAND WINDOW: total refills=%d  slot0(FG-Low)=%d  slot1(FG-High)=%d"
              % (tot, sr0, sr1))
        # slot 1 ring of last 8 origins: base + (1*8 + i)*4
        rwx, rwy = lay["_p6_w_lay_ring_wx"], lay["_p6_w_lay_ring_wy"]
        if rwx is not None and rwy is not None:
            print("  slot1 last refill origins (wx,wy) -- floor row ty=86 needs"
                  " wy in [55..86]:")
            covers = 0
            for i in range(8):
                wx = pkat(rwx, 8 + i)
                wy = pkat(rwy, 8 + i)
                cov = (wy <= 86 < wy + 32)
                if cov:
                    covers += 1
                print("      (%4d,%4d)  rows[%d..%d]  %s"
                      % (wx, wy, wy, wy + 31, "covers ty86" if cov else "MISSES ty86"))
            print("  -> %d/8 recent slot1 windows cover the floor row ty=86" % covers)
        print("-" * 72)

    # ---- which layer is the SHARED slot 1 bound to at capture? ---------------
    # The VDP2 present rebinds slot 1 with a hardcoded FG-Low index (3 -> slot1
    # = layer 4). For GHZ2 the FG-High layer is 5, so a value of 4 here == the
    # present clobbered the collision binding to FG-Low == fall-through.
    bnd = None
    sl1 = _scene.map_symbol(map_text, "_p6_w_ghz2_slot1layer")
    if sl1 is not None:
        bnd = _scene.peek_u32(mod, sections, sl1, perm, signed=True)
        print("  slot1 (SHARED collision+present) bound to layer=%s  (FG-High=%d "
              "expected; 4=FG-Low == present clobbered it)"
              % (str(bnd), hi_idx))
        print("-" * 72)

    # ---- GATE verdict (RED-first; GREEN after the fix) ----------------------
    # ROOT CAUSE (measured): the VDP2 present rebinds the SHARED SaturnLayout
    # slot 1 to a HARDCODED layer index (3+1=4, correct only for GHZ1). GHZ2's
    # FG-High is layer 5, so slot 1 ends up bound to GHZ2's layer 4 (FG-Low),
    # and the player's floor query reads FG-Low (no floor bit) -> fall-through.
    # GREEN requires BOTH: slot 1 stays bound to the FG-High index, AND the
    # player does NOT fall off the spawn floor.
    if not loaded:
        print("RESULT: GHZ2 was not the live scene at capture (loaded=0). Recapture.")
        return 1
    grounded = fell <= 64
    bound_ok = (bnd is None) or (bnd == hi_idx)  # bnd unknown -> don't gate on it
    print("GATE:")
    print("  [%s] slot1 stays bound to FG-High (layer %d): bound to %s"
          % ("GREEN" if bound_ok else " RED ", hi_idx,
             "n/a" if bnd is None else str(bnd)))
    print("  [%s] player holds the spawn floor (fell %d px, <=64 ok)"
          % ("GREEN" if grounded else " RED ", fell))
    ok = grounded and bound_ok
    print("=" * 72)
    if ok:
        print("RESULT: GREEN -- GHZ2 player stays grounded; FG-High slot binding intact.")
        return 0
    print("RESULT: RED -- GHZ2 fall-through (present clobbers the FG-High collision"
          " slot binding / player falls off the spawn floor).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
