#!/usr/bin/env python3
# =============================================================================
# qa_p6_sprresid.py -- Task #241 gate: VDP1 sprite RESIDENCY (characters blink /
# drop out while moving AND standing still).
#
# ROOT CAUSE (measured, not guessed): the VDP1 slot cache (p6_vdp1.c) is
# FILL-ONCE with NO eviction (P6_VDP1_NSLOTS slots; p6_slot_for increments
# p6_w_vdp1_drops and returns -1 once full). Across a run the first NSLOTS
# distinct sheet rects lock in permanently; every later rect -- including the
# player's current animation frame -- DROPS, so the player blits skip and the
# sprite blinks out. This is DISTINCT from the W18 unbound-surface drop class
# (p6_w_vdp1_handle_drops / p6_w_dropbysheet[sheetID], where the surface was
# never bound at all). The fix is LRU eviction (a cache MISS becomes evict +
# jo_sprite_replace + draw, never a dropped blit).
#
# WITNESS CONTRACT
#   p6_w_vdp1_slots        distinct rects currently resident (== NSLOTS == 40
#                          when the fill-once cache is SATURATED).
#   p6_w_vdp1_drops        slot-cache-exhaustion drops. >0 == the bug. The fix
#                          drives this to 0 (every bound rect always gets a
#                          slot via eviction).
#   p6_w_vdp1_landed       blits that reached a valid VDP1 slot (sanity: >0).
#   p6_w_vdp1_handle_drops UNBOUND-surface drops (separate residency class;
#                          reported for attribution, NOT gated here).
#   p6_w_dropbysheet[0..15] per-surface unbound-drop histogram (attribution).
#   p6_w_cont_frames       continuous-GHZ frame counter (sanity: >0 == we
#                          captured a live GHZ, not the boot/load screen).
#
# RED  (current build): p6_w_vdp1_drops > 0  (the fill-once cache saturates).
# GREEN (after LRU fix): p6_w_vdp1_drops == 0 AND p6_w_vdp1_landed > 0 AND
#                        we are in a live GHZ (cont_frames > 0).
#
# Usage: python tools/_portspike/qa_p6_sprresid.py [savestate.mcs] [game.map]
# =============================================================================
import importlib.util
import os
import pathlib
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

MCS_DEFAULT = os.path.join(HERE, "p6_sprresid_walk.mcs")

# Scalar witnesses (leading underscore = sh-none-elf convention; map_symbol
# tolerant of either form).
SYMS = [
    "_p6_w_vdp1_slots", "_p6_w_vdp1_drops", "_p6_w_vdp1_landed",
    "_p6_w_vdp1_handle_drops", "_p6_w_vdp1_joaddfail",
    "_p6_w_cont_frames", "_p6_lean_boot",
    "_p6_w_plr_animid", "_p6_w_plr_drawflags",
]
DROPBYSHEET = "_p6_w_dropbysheet"   # int32[16] histogram base


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("TASK #241 GATE: VDP1 sprite residency (character blink / drop-out)")
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
    base_dbs = _scene.map_symbol(map_text, DROPBYSHEET)
    if missing:
        print("RESULT: RED -- witness symbol(s) absent from the map:")
        for s in missing:
            print("        %s" % s)
        return 1
    if not os.path.isfile(mcs):
        print("RESULT: RED -- savestate missing (%s)" % mcs)
        return 1

    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    label, perm = _scene.calibrate(mod._peek_bytes(sections, syms[_scene.SYM_MAGIC], 4))
    if perm is None:
        print("RESULT: RED -- magic mis-decode (image did not load / wrong bank)")
        return 1

    v = {s: _scene.peek_u32(mod, sections, syms[s], perm, signed=True) for s in SYMS}
    # Optional (reported, not gated): proves the LRU eviction path is live.
    ev_sym = _scene.map_symbol(map_text, "_p6_w_vdp1_evicts")
    evicts = _scene.peek_u32(mod, sections, ev_sym, perm, signed=True) if ev_sym is not None else None
    dbs = []
    if base_dbs is not None:
        for i in range(16):
            dbs.append(_scene.peek_u32(mod, sections, base_dbs + 4 * i, perm, signed=True) or 0)

    slots   = v["_p6_w_vdp1_slots"]
    drops   = v["_p6_w_vdp1_drops"]
    landed  = v["_p6_w_vdp1_landed"]
    hdrops  = v["_p6_w_vdp1_handle_drops"]
    joaf    = v["_p6_w_vdp1_joaddfail"]
    cont    = v["_p6_w_cont_frames"]
    lean    = v["_p6_lean_boot"]
    animid  = v["_p6_w_plr_animid"]
    dflags  = v["_p6_w_plr_drawflags"]

    print("  savestate : %s   (calib=%s)" % (mcs, label))
    print("  flavor    : lean_boot=%s  cont_frames=%s" % (lean, cont))
    print("  player    : animID=%s  drawflags=0x%08X (group<<16|vis<<8|onScreen)"
          % (animid, (dflags or 0) & 0xFFFFFFFF))
    print("-" * 72)
    print("  VDP1 slot cache:")
    print("    p6_w_vdp1_slots        = %s   (distinct rects resident; ==40 == SATURATED)" % slots)
    print("    p6_w_vdp1_drops        = %s   (slot-cache exhaustion -- THE BUG; fix -> 0)" % drops)
    print("    p6_w_vdp1_landed       = %s   (blits that reached a slot)" % landed)
    print("    p6_w_vdp1_evicts       = %s   (LRU evictions -- >0 proves the fix is live)" % evicts)
    print("    p6_w_vdp1_handle_drops = %s   (UNBOUND surface; separate class)" % hdrops)
    print("    p6_w_vdp1_joaddfail    = %s" % joaf)
    if base_dbs is not None:
        nz = [(i, c) for i, c in enumerate(dbs) if c]
        print("    dropbysheet[unbound]   = %s" % (nz if nz else "(all zero -- every surface bound)"))
    print("-" * 72)

    # Gate logic.
    sane_live = cont is not None and cont > 0
    sane_draw = landed is not None and landed > 0
    fixed     = drops is not None and drops == 0

    print("  [%s] G1 captured a LIVE GHZ (cont_frames > 0)"
          % ("GREEN" if sane_live else " RED "))
    print("  [%s] G2 blits are landing (p6_w_vdp1_landed > 0)"
          % ("GREEN" if sane_draw else " RED "))
    print("  [%s] G3 NO slot-cache drops (p6_w_vdp1_drops == 0)"
          % ("GREEN" if fixed else " RED "))
    print("-" * 72)
    if sane_live and sane_draw and fixed:
        print("RESULT: GREEN -- every bound sprite rect gets a VDP1 slot via")
        print("        eviction; the player no longer drops/blinks.")
        return 0
    if not sane_live:
        print("RESULT: RED -- not a live GHZ capture (cont_frames=%s). Recapture" % cont)
        print("        deeper (the P6_CART load is ~110-130s realtime).")
        return 1
    print("RESULT: RED -- slot-cache drops present (drops=%s, slots=%s)." % (drops, slots))
    print("        The fill-once cache saturates; bound rects drop -> blink.")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
