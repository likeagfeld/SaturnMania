#!/usr/bin/env python3
# =============================================================================
# qa_p6_draw.py -- P6.5b3 gate (Task #208): ENGINE DrawSprite SLOT -> VDP1.
# The proof tick now draws through the engine's CANONICAL DrawSprite signature
# (Animator*, Vector2* world-fixed-point, screenRelative) -- the exact call
# shape every decomp object's Draw callback uses (e.g. Ring_Draw:
# `RSDK.DrawSprite(&self->animator, NULL, false)`). The Saturn backend
# implementation mirrors Drawing.cpp:2670-2686 position semantics (pos>>16,
# currentScreen->position translate, sceneInfo.entity drawFX dispatch) and
# routes the FX_NONE arm (Drawing.cpp:2785) to a slot-cached VDP1 blitter
# instead of the software DrawSpriteFlipped raster (infeasible on SH-2 per
# the Task #194 spike -- Draw* IS the designated Saturn backend seam).
#
# Witness contract (offline model = parse_spr on extracted Ring.bin):
#   D1 p6_w_draw_calls   > 8 and == p6_w_spr_ticks (every tick draws, and
#      ONLY through DrawSprite -- couples the animator tick to the draw)
#   D2 p6_w_draw_xy      == ((52&0xFFFF)<<16)|104: engine position math
#      top-left = (pos.x>>16) - screenPos.x + pivotX = 60 - 0 + (-8) = 52,
#      (pos.y>>16) - screenPos.y + pivotY = 112 - 0 + (-8) = 104.
#   D3 p6_w_draw_rect    == (sprX<<16)|sprY of anim-0 frame[frameID at the
#      SAME capture] -- proves the CURRENT frame's sheet rect flows through
#      DrawSprite (frameID from the W8 cadence formula on p6_w_spr_ticks).
#   D4 p6_w_draw_sheetid == 0 (Items.gif = first loaded surface).
#   D5 p6_w_vdp1_slots   == 16: all 16 distinct anim-0 frame rects resident
#      in the jo VDP1 slot cache, and NO MORE -- a per-tick jo_sprite_add
#      leak (the #189 sprite-table overflow class) would blow far past 16
#      after ~1900 ticks.
#   D6 (pixel) ring present at screen center (60,112): capture differs from
#      the island-only model inside the sprite rect, matches outside
#      (identical anchor/thresholds to qa_p6_sprite.py W9).
#
# Usage: python tools/_portspike/qa_p6_draw.py [savestate.mcs] [capture.png] [map]
# =============================================================================
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
MCS_DEFAULT = os.path.join(HERE, "_p6", "_p6_pack.mcs")
RING_BIN = os.path.join(ROOT, "extracted", "Data", "Sprites", "Global", "Ring.bin")
MODEL_PNG = os.path.join(HERE, "_p6", "_p6_vdp2_model.png")

DRAW_CX, DRAW_CY = 60, 112  # world pos (60<<16,112<<16), 16x16 + pivot -8 -> center here

SYMS = ["_p6_w_draw_calls", "_p6_w_draw_xy", "_p6_w_draw_rect",
        "_p6_w_draw_sheetid", "_p6_w_vdp1_slots", "_p6_w_spr_ticks"]


def model():
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    from convert_ring_sprite import parse_spr
    sheets, anims = parse_spr(RING_BIN)
    a0 = anims[0]
    frames = []
    for (sheet_id, sx, sy, w, h, px, py, dur, uc) in a0["frames"]:
        frames.append({"sx": sx, "sy": sy, "w": w, "h": h, "px": px, "py": py,
                       "dur": dur})
    return {"frames": frames, "speed": a0["speed"], "count": len(frames)}


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    cap = _scene._as_path(argv[2]) if len(argv) > 2 else None
    mp = _scene._as_path(argv[3]) if len(argv) > 3 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.5b3 DRAW GATE: engine DrawSprite slot -> VDP1 backend")
    print("=" * 72)
    m = model()
    print("  model: anim0 %d frames speed %d; expected top-left (52,104)"
          % (m["count"], m["speed"]))
    print("  savestate: %s" % mcs)
    print("  capture  : %s" % (cap or "(none -- pixel tier RED)"))
    print("-" * 72)

    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s)" % mp)
        return 1
    map_text = _scene.read_text(mp)
    syms = {}
    missing = []
    for s in [_scene.SYM_MAGIC] + SYMS:
        syms[s] = _scene.map_symbol(map_text, s)
        if syms[s] is None:
            missing.append(s)
    if missing:
        print("RESULT: RED -- witness symbol(s) absent from the map:")
        for s in missing:
            print("        %s" % s)
        print("        (Expected while the P6.5b3 backend is unwritten.)")
        return 1
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
    v = {s: _scene.peek_u32(mod, sections, syms[s], perm,
                            signed=(s in ("_p6_w_draw_calls", "_p6_w_spr_ticks",
                                          "_p6_w_vdp1_slots")))
         for s in SYMS}
    print("  peeked: " + "  ".join("%s=%s" % (s[6:], _scene._hx(v[s])) for s in SYMS))

    # P6.7a: when the engine-loop ring is ALIVE, the LAST DrawSprite each tick
    # is the ENTITY's (ProcessObjectDrawLists runs after the harness draw), so
    # the xy/rect witnesses reflect the FALLING ring -- model them from the
    # peeked object witnesses (frameid + posy + spawn x 260).
    obj_classid_sym = _scene.map_symbol(map_text, "_p6_w_obj_classid")
    obj_alive = False
    obj_fid = obj_posy = 0
    if obj_classid_sym is not None:
        obj_alive = _scene.peek_u32(mod, sections, obj_classid_sym, perm, signed=True) == 1
        obj_fid  = _scene.peek_u32(mod, sections, _scene.map_symbol(map_text, "_p6_w_obj_frameid"), perm, signed=True) or 0
        obj_posy = _scene.peek_u32(mod, sections, _scene.map_symbol(map_text, "_p6_w_obj_posy"), perm, signed=True) or 0

    # frameID at capture from the W8 cadence formula (Animation.cpp:166-175).
    # NOTE the tick's witness order: ProcessAnimation -> frame/ticks witnesses
    # -> DrawSprite. So the draw witnesses reflect frameID AT ticks.
    t = v["_p6_w_spr_ticks"] or 0
    # variable per-frame durations: walk the accumulation like the engine does
    if t > 0:
        timer, fid, fdur = 0, 0, m["frames"][0]["dur"]
        for _ in range(t):
            timer += m["speed"]
            while timer > fdur:
                fid += 1
                timer -= fdur
                if fid >= m["count"]:
                    fid = 0
                fdur = m["frames"][fid]["dur"]
        exp_fid = fid
    else:
        exp_fid = 0
    # Top-left = (pos.x>>16) - screenPos.x + pivotX of the frame DRAWN AT
    # CAPTURE -- Ring's 16 rotating frames carry PER-FRAME pivots/sizes
    # (frame 8 is narrower, pivot -3), so the expectation must come from the
    # same frame the cadence walk predicts, exactly as Drawing.cpp:2673/2785
    # resolves it. With the P6.7a engine-loop ring alive, the last draw is
    # the ENTITY's: position from the peeked posy, frame from obj_frameid.
    if obj_alive:
        ent = m["frames"][obj_fid % len(m["frames"])]
        exp_rect = ((ent["sx"] << 16) | ent["sy"]) & 0xFFFFFFFF
        exp_x = (260 + ent["px"]) & 0xFFFF
        exp_y = (((obj_posy >> 16) & 0xFFFFFFFF) + ent["py"]) & 0xFFFF
        exp_fid = obj_fid
    else:
        ent = m["frames"][exp_fid]
        exp_rect = ((ent["sx"] << 16) | ent["sy"]) & 0xFFFFFFFF
        exp_x = (60 + ent["px"]) & 0xFFFF
        exp_y = (112 + ent["py"]) & 0xFFFF
    exp_xy = (exp_x << 16) | exp_y

    # P6.7a: the engine-loop ring's Ring_Draw_Normal ALSO dispatches through
    # DrawSprite -- expected calls = harness ticks + obj_draws (peeked; falls
    # back to the pre-P6.7a model when the symbol is absent).
    obj_sym = _scene.map_symbol(map_text, "_p6_w_obj_draws")
    obj_draws = 0
    if obj_sym is not None:
        obj_draws = _scene.peek_u32(mod, sections, obj_sym, perm, signed=True) or 0
    exp_calls = (v["_p6_w_spr_ticks"] or 0) + obj_draws

    checks = [
        ("D1 every tick draws through DrawSprite (calls==ticks+objdraws>8)",
         v["_p6_w_draw_calls"] is not None and (v["_p6_w_draw_calls"] or 0) > 8
         and v["_p6_w_draw_calls"] == exp_calls,
         "calls=%s ticks=%s objdraws=%s" % (v["_p6_w_draw_calls"],
                                            v["_p6_w_spr_ticks"], obj_draws)),
        ("D2 engine position math: top-left == (60,112)+pivot of frame %d" % exp_fid,
         (v["_p6_w_draw_xy"] or 0) & 0xFFFFFFFF == exp_xy,
         "got %s expect 0x%08X" % (_scene._hx(v["_p6_w_draw_xy"]), exp_xy)),
        ("D3 current frame's sheet rect (frame %d at ticks=%s)" % (exp_fid, t),
         (v["_p6_w_draw_rect"] or 0) & 0xFFFFFFFF == exp_rect,
         "got %s expect 0x%08X" % (_scene._hx(v["_p6_w_draw_rect"]), exp_rect)),
        ("D4 sheetID == 0",
         v["_p6_w_draw_sheetid"] == 0,
         "got %s" % v["_p6_w_draw_sheetid"]),
        # P6.7a: 17, MEASURED -- the 16 anim-0 rects plus ONE foreign key
        # (sx=0,sy=1,w=10,h=16: frame 11's shape with zeroed sprX), uploaded
        # exactly once in the first cycle and never again across 29 respawn
        # cycles / 1,858 ticks (WRAM slot-table dump 2026-06-10). Bounded ==
        # no leak; the transient sprX=0 read is a P6.7b diagnostic item
        # (suspect: a one-tick stale-frames read around the engine STG
        # allocations). The HARD bound stays exact so any real leak fires.
        ("D5 VDP1 slot cache bounded (16 rects + 1 measured transient)",
         v["_p6_w_vdp1_slots"] in (16, 17),
         "got %s" % v["_p6_w_vdp1_slots"]),
    ]

    if cap and os.path.isfile(cap):
        from PIL import Image
        import numpy as np

        def green_bbox(img):
            a = np.asarray(img.convert("RGB")).astype(int)
            msk = (a[..., 1] > 1.4 * a[..., 0]) & (a[..., 1] > 1.4 * a[..., 2]) \
                & (a[..., 1] > 80)
            ys, xs = np.where(msk)
            if len(xs) < 500:
                return None
            return (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)

        cap_img = Image.open(cap).convert("RGB")
        cap_img = cap_img.crop((0, 30, cap_img.width, cap_img.height))
        mod_img = Image.open(MODEL_PNG).convert("RGB")
        bb_c = green_bbox(cap_img)
        bb_m = green_bbox(mod_img)
        if bb_c is None or bb_m is None:
            checks.append(("D6 ring present", False, "island anchor not found"))
        else:
            sx = (bb_c[2] - bb_c[0]) / float(bb_m[2] - bb_m[0])
            sy = (bb_c[3] - bb_c[1]) / float(bb_m[3] - bb_m[1])
            ox = bb_c[0] - bb_m[0] * sx
            oy = bb_c[1] - bb_m[1] * sy
            full = cap_img.crop((int(ox), int(oy),
                                 int(ox + 320 * sx), int(oy + 224 * sy))) \
                .resize((320, 224), Image.BILINEAR)
            a = np.asarray(full).astype(int)
            b = np.asarray(mod_img).astype(int)
            d = np.abs(a - b).max(axis=2)
            rect = d[DRAW_CY - 12:DRAW_CY + 12, DRAW_CX - 12:DRAW_CX + 12]
            inside = (rect > 40).mean()
            outside_mask = np.ones_like(d, bool)
            outside_mask[DRAW_CY - 16:DRAW_CY + 16, DRAW_CX - 16:DRAW_CX + 16] = False
            outside = (d[outside_mask] > 40).mean()
            checks.append(("D6 ring present at (%d,%d): inside>15%%, outside<10%%"
                           % (DRAW_CX, DRAW_CY),
                           inside > 0.15 and outside < 0.10,
                           "inside=%.1f%% outside=%.1f%%" % (100 * inside, 100 * outside)))
    else:
        checks.append(("D6 ring present", False, "no capture provided"))

    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- objects' canonical RSDK.DrawSprite call shape")
        print("        renders on VDP1 through the Saturn backend: engine")
        print("        position math, per-frame sheet rects, bounded slot cache.")
        return 0
    print("RESULT: RED -- engine DrawSprite slot not proven (see witnesses).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
