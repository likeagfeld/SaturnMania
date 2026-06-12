#!/usr/bin/env python3
# =============================================================================
# qa_p6_scroll.py -- P6.7 W16 gate (Task #228): the ENGINE-loaded GHZ1
# FOREGROUND (FG Low, layer 3) VISIBLE on VDP2 NBG1, anchored to the LIVE
# camera (screens[0].position after the 60 engine ticks == (0, 780),
# Camera_SetCameraBounds bounds-clamp -- W15 measured, p6_w_scr_x/y).
#
# Register/VRAM contracts (ST-058-R2; addresses verified against
# sega_saturn_docs/VDP2_Manual.txt:5405-5425 + :5472-5474):
#   SCXIN1 (NBG1 horizontal scroll, integer part, bits 10~0) = 0x05F80080
#   SCYIN1 (NBG1 vertical scroll, integer part, bits 10~0)   = 0x05F80084
#   NOTE the task brief named 0x...70/0x...74 -- those are NBG0's
#   (N0SCXI/N0SCYI, VDP2_Manual.txt:5374/5390). NBG1 is 0x80/0x84.
#   NBG1 PND map: 4 pages x 32x32 x 2-word PNDs at VDP2 VRAM B0 base
#   0x05E40000 (the proven p6_vdp2.c geometry, P6.5b1; SaturnSheet's W12a
#   band stores start at 0x05E44000 -- clear of the 16 KB map).
#
# OFFLINE MODEL: extracted/Data/Stages/GHZ/Scene1.bin "FG Low" layout
# (build_layout_bands.parse_layers -- the SAME bytes the cd/GHZ1LAYT.BIN
# band store carries, byte-exact per qa_p6_layout L2). PND formula =
# p6_vdp2.c:105-119 (fy bit11->31, fx bit10->30, charno = tile*8, empty
# 0xFFFF -> blank char = smallest tile index unreferenced by the 64x64
# rect, its cells zeroed).
#
# CHECKS
#   S1 witness symbols present in game.map (RED while W16 unimplemented).
#   S2 NBG1 scroll registers == f(camera): SCXIN1/SCYIN1 (savestate
#      RawRegs) == the witnessed scroll (p6_w_scr2_x/y) == the live camera
#      witnesses (p6_w_scr_x/y) == the (0, 780) model.
#   S3 PND window byte-exact: the 16,384 B NBG1 map in savestate VDP2 VRAM
#      == the offline model image, AND the SH-2 read-back hash witness
#      (p6_w_scr2_pndhash) == djb2 over the same model bytes.
#   S4 visible pixels sanity: non-empty FG tiles in the visible 320x224
#      window (rows 48..62, cols 0..19 at scroll (0,780)): witness ==
#      model count > 0, AND the savestate map carries the same count of
#      non-blank charnos in that window.
#
# Usage: python tools/_portspike/qa_p6_scroll.py [savestate.mcs] [map]
# =============================================================================
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

_spec2 = importlib.util.spec_from_file_location(
    "blb", os.path.join(ROOT, "tools", "build_layout_bands.py"))
_blb = importlib.util.module_from_spec(_spec2)
_spec2.loader.exec_module(_blb)

MCS_DEFAULT = os.path.join(HERE, "p6_i0.mcs")
SCENE_BIN = os.path.join(ROOT, "extracted", "Data", "Stages", "GHZ",
                         "Scene1.bin")

SYMS = ["_p6_w_scr2_x", "_p6_w_scr2_y", "_p6_w_scr2_pndhash",
        "_p6_w_scr2_nblank", "_p6_w_scr2_done"]
CAM_SYMS = ["_p6_w_scr_x", "_p6_w_scr_y"]

# W15 measured camera rest position (Camera_SetCameraBounds clamp:
# cameraBounds 16384x1004 px, screen 320x224 -> y = 1004-224 = 780).
EXP_SCROLL = (0, 780)
VDP2_VRAM = 0x05E00000
NBG1_MAP = VDP2_VRAM + 0x40000  # B0 base, 16,384 B (p6_vdp2.c P6_VDP2_MAP)
SCXIN1 = 0x05F80080
SCYIN1 = 0x05F80084
RECT = 64               # 64x64-tile plane window (PL_SIZE_2x2, 4 pages)
SCREEN_W, SCREEN_H = 320, 224
FG_LOW_NAME = "FG Low"  # tileLayers[3] / band-store layer 3


def djb2(data):
    h = 5381
    for b in data:
        h = (((h << 5) + h) ^ b) & 0xFFFFFFFF
    return h


def build_model():
    """64x64 PND map image (Saturn-visible big-endian bytes) + blank char +
    the visible-window non-empty count, from the original Scene.bin."""
    layers = _blb.parse_layers(SCENE_BIN)
    fg = [(n, xs, ys, lay) for (n, xs, ys, lay) in layers if n == FG_LOW_NAME]
    if len(fg) != 1:
        raise SystemExit("model error: FG Low not found in %s" % SCENE_BIN)
    name, xs, ys, lay = fg[0]

    def word(tx, ty):
        if tx < 0 or ty < 0 or tx >= xs or ty >= ys:
            return 0xFFFF
        o = (ty * xs + tx) * 2
        return lay[o] | (lay[o + 1] << 8)  # layout words are LE on disk

    used = set()
    for y in range(RECT):
        for x in range(RECT):
            e = word(x, y)
            if e != 0xFFFF:
                used.add(e & 0x3FF)
    blank = 0
    while blank < 1024 and blank in used:
        blank += 1

    img = bytearray(RECT * RECT * 4)
    for y in range(RECT):
        for x in range(RECT):
            e = word(x, y)
            if e == 0xFFFF:
                hi, lo = 0, blank * 8
            else:
                hi = (((e >> 11) & 1) << 15) | (((e >> 10) & 1) << 14)
                lo = (e & 0x3FF) * 8
            page = ((y >> 5) << 1) + (x >> 5)
            o = (page * 2048 + (((y & 31) << 5) + (x & 31)) * 2) * 2
            img[o + 0] = hi >> 8
            img[o + 1] = hi & 0xFF
            img[o + 2] = lo >> 8
            img[o + 3] = lo & 0xFF

    sx, sy = EXP_SCROLL
    tx0, tx1 = sx >> 4, (sx + SCREEN_W - 1) >> 4
    ty0, ty1 = sy >> 4, (sy + SCREEN_H - 1) >> 4
    nblank = sum(1 for y in range(ty0, ty1 + 1) for x in range(tx0, tx1 + 1)
                 if word(x, y) != 0xFFFF)
    return bytes(img), blank, nblank, (tx0, tx1, ty0, ty1)


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.7 W16 SCROLL GATE: GHZ1 FG Low on NBG1, anchored to the live camera")
    print("=" * 72)

    model_img, blank, model_nblank, win = build_model()
    model_hash = djb2(model_img)
    print("  model: blank char %d, PND map djb2 0x%08X, visible window "
          "tx %d..%d ty %d..%d non-empty %d"
          % (blank, model_hash, win[0], win[1], win[2], win[3], model_nblank))

    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s)" % mp)
        return 1
    map_text = _scene.read_text(mp)
    syms = {}
    missing = []
    for s in [_scene.SYM_MAGIC] + SYMS + CAM_SYMS:
        syms[s] = _scene.map_symbol(map_text, s)
        if syms[s] is None:
            missing.append(s)
    if missing:
        print("  [ RED ] S1 witness symbols present in the link map")
        for s in missing:
            print("          MISSING %s" % s)
        print("          (Expected while W16 is unimplemented.)")
        print("-" * 72)
        print("RESULT: RED -- W16 not proven (S1).")
        return 1
    print("  [GREEN] S1 witness symbols present in the link map")
    if not os.path.isfile(mcs):
        print("RESULT: RED -- savestate missing (%s)" % mcs)
        return 1

    import pathlib
    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    raw_magic = mod._peek_bytes(sections, syms[_scene.SYM_MAGIC], 4)
    label, perm = _scene.calibrate(raw_magic)
    if perm is None:
        print("RESULT: RED -- magic mis-decode")
        return 1
    v = {s: _scene.peek_u32(mod, sections, syms[s], perm, signed=True)
         for s in SYMS + CAM_SYMS}

    def peek16(addr):
        raw = mod._peek_bytes(sections, addr, 2)
        if raw is None or len(raw) < 2:
            return None
        return (raw[0] << 8) | raw[1]  # Saturn-visible big-endian

    reg_x = peek16(SCXIN1)
    reg_y = peek16(SCYIN1)
    vram = mod._peek_bytes(sections, NBG1_MAP, len(model_img))
    vram = bytes(vram) if vram is not None else b""

    # savestate-side visible-window census (S4 second leg): count non-blank
    # charnos inside the window from the captured map itself.
    ss_nblank = -1
    if len(vram) == len(model_img):
        n = 0
        for y in range(win[2], win[3] + 1):
            for x in range(win[0], win[1] + 1):
                page = ((y >> 5) << 1) + (x >> 5)
                o = (page * 2048 + (((y & 31) << 5) + (x & 31)) * 2) * 2
                lo = (vram[o + 2] << 8) | vram[o + 3]
                if lo != blank * 8:
                    n += 1
        ss_nblank = n

    sx, sy = v["_p6_w_scr2_x"], v["_p6_w_scr2_y"]
    s2 = (reg_x is not None and reg_y is not None
          and sx is not None and sy is not None
          and reg_x & 0x7FF == sx & 0x7FF and reg_y & 0x7FF == sy & 0x7FF
          and (sx, sy) == (v["_p6_w_scr_x"], v["_p6_w_scr_y"])
          and (sx, sy) == EXP_SCROLL
          and v["_p6_w_scr2_done"] == 1)
    s3 = (vram == model_img
          and (v["_p6_w_scr2_pndhash"] or 0) & 0xFFFFFFFF == model_hash)
    s4 = (v["_p6_w_scr2_nblank"] == model_nblank and model_nblank > 0
          and ss_nblank == model_nblank)

    checks = [
        ("S2 NBG1 scroll registers == camera (SCXIN1/SCYIN1 == witness == "
         "screens[0].position == (0,780))", s2,
         "SCXIN1=%s SCYIN1=%s scr2=(%s,%s) cam=(%s,%s) done=%s"
         % (reg_x, reg_y, sx, sy, v["_p6_w_scr_x"], v["_p6_w_scr_y"],
            v["_p6_w_scr2_done"])),
        ("S3 NBG1 PND window byte-exact vs the offline FG Low model "
         "(16,384 B + read-back hash)", s3,
         "vram %s model djb2 0x%08X witness 0x%08X firstdiff %s"
         % ("match" if vram == model_img else "MISMATCH(len %d)" % len(vram),
            model_hash, (v["_p6_w_scr2_pndhash"] or 0) & 0xFFFFFFFF,
            next((i for i in range(min(len(vram), len(model_img)))
                  if vram[i] != model_img[i]), None))),
        ("S4 visible pixels sanity (non-empty FG tiles in the 320x224 "
         "window > 0, witness == savestate == model)", s4,
         "witness=%s savestate=%s model=%d"
         % (v["_p6_w_scr2_nblank"], ss_nblank, model_nblank)),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the engine-loaded GHZ1 FG Low layer is staged")
        print("        on VDP2 NBG1 byte-exact (cells + camera-local PND page)")
        print("        with the scroll registers anchored to the live camera.")
        return 0
    print("RESULT: RED -- W16 not proven (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
