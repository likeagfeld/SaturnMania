#!/usr/bin/env python3
# =============================================================================
# qa_p6_fgdma.py -- Task #242 gate: the GHZ foreground PND map reaches VDP2 via
# SCU DMA in the vblank (tear-free), NOT CPU stores during active display.
#
# ROOT CAUSE (user "chunks of grass missing while moving"): the present wrote the
# VDP2 pattern-name map with CPU stores during ACTIVE DISPLAY, which tear / land
# partially (hand-port src/_archived/main_streaming_WORKING.c.bak:9-12; ST-210
# SCU; memory saturn-vdp2-streaming-solved). FIX: build the 64x64 page in a CART
# (A-Bus) buffer, slDMAXCopy it to the VDP2 map in p6_fg_vblank (the proven in-
# repo src/rsdk/scene_ghz.c ghz_fg_vblank pattern).
#
# CHECKS
#   D1 the DMA witness symbol _p6_w_fg_dma is present in game.map. RED on the
#      pre-fix build (the present still CPU-stores; the symbol does not exist).
#   D2 the vblank DMA actually ran: p6_w_fg_dma > 0 (page DMA'd at least once).
#   D3 plane content sane at a moved camera: p6_w_fg_visok_far == 1.
#
# NOTE: the AUTHORITATIVE tearing check is a DENSE in-motion PIXEL capture
# (tools/_portspike/qa_motion_shots.ps1) -- visok checks VRAM CONTENT and can
# pass while the DISPLAY tears, which is exactly why the earlier gate lied. This
# gate proves the MECHANISM changed (CPU store -> SCU DMA); the pixel capture
# proves the holes are gone.
#
# Usage: python tools/_portspike/qa_p6_fgdma.py [savestate.mcs] [map]
# =============================================================================
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

MCS_DEFAULT = os.path.join(HERE, "p6_fgdma.mcs")
SYMS = ["_p6_w_fg_dma", "_p6_w_fg_visok_far", "_p6_w_fg_maxcamtx"]


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("TASK #242 GATE: GHZ foreground map reaches VDP2 via vblank SCU DMA")
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
    if missing:
        print("  [ RED ] D1 DMA witness symbols present in the link map")
        for s in missing:
            print("          MISSING %s" % s)
        print("          (Expected RED while the present still CPU-stores the map.)")
        print("RESULT: RED -- vblank SCU-DMA path not present (D1).")
        return 1
    print("  [GREEN] D1 DMA witness symbols present in the link map")
    if not os.path.isfile(mcs):
        print("RESULT: RED -- savestate missing (%s)" % mcs)
        return 1

    import pathlib
    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    perm = _scene.calibrate(mod._peek_bytes(sections, syms[_scene.SYM_MAGIC], 4))[1]
    if perm is None:
        print("RESULT: RED -- magic mis-decode")
        return 1
    v = {s: _scene.peek_u32(mod, sections, syms[s], perm, signed=True) for s in SYMS}
    dma = v["_p6_w_fg_dma"]
    far = v["_p6_w_fg_visok_far"]
    mx = v["_p6_w_fg_maxcamtx"]
    d2 = dma is not None and dma > 0
    d3 = far is not None and far == 1
    print("-" * 72)
    print("  [%s] D2 vblank page DMA ran (p6_w_fg_dma > 0)" % ("GREEN" if d2 else " RED "))
    print("          fg_dma=%s  (max_camtx=%s)" % (dma, mx))
    print("  [%s] D3 plane content matched layout at a moved camera (visok_far==1)"
          % ("GREEN" if d3 else " RED "))
    print("          visok_far=%s" % far)
    print("-" * 72)
    if d2 and d3:
        print("RESULT: GREEN -- the FG map is uploaded by SCU DMA in vblank.")
        print("        (Confirm holes-gone with the dense in-motion pixel capture.)")
        return 0
    print("RESULT: RED -- vblank DMA not confirmed.")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
