#!/usr/bin/env python3
# =============================================================================
# qa_p6_sprite.py -- P6.5b2 gate (Task #208): ENGINE OBJECT SPRITES ON VDP1.
# The UNMODIFIED engine loads the REAL Global/Ring.bin animation set from the
# ORIGINAL Data.rsdk (LoadSpriteAnimation -> LoadSpriteSheet -> ImageGIF, all
# engine code), ticks it with the engine's own ProcessAnimation, and the
# Saturn render backend draws the current frame on VDP1 over the P6.5b1
# engine-rendered Island layer.
#
# Witness contract (offline model = tools/convert_ring_sprite.py parse_spr on
# extracted/Data/Sprites/Global/Ring.bin -- field order verified IDENTICAL to
# Animation.cpp:86-94):
#   W1 p6_w_spr_id        != -1 (LoadSpriteAnimation slot)
#   W2 p6_w_spr_animcount == 5  ("Normal Ring", "Hyper Ring", Sparkle 1-3)
#   W3 p6_w_spr_f0xy      == (sprX<<16)|sprY     of anim0 frame0 (model 1,1)
#   W4 p6_w_spr_f0wh      == (width<<16)|height  (model 16,16)
#   W5 p6_w_spr_f0pv      == (pivotX&0xFFFF)<<16 | (pivotY&0xFFFF) (model -8,-8)
#   W6 p6_w_spr_f0dur     == duration (model 256)
#   W7 p6_w_spr_sheethash == djb2-xor over gfxSurface[sheet].pixels
#                            (32,768 B; model = Pillow decode of Items.gif --
#                            palette-INDEPENDENT index bytes)
#   W8 animator cadence: p6_w_spr_frame == (p6_w_spr_ticks * speed / duration)
#                        % frameCount for anim 0 (speed 64, duration 256,
#                        16 frames -> one frame per 4 ticks) -- proves the
#                        engine's ProcessAnimation is ticking the animator.
#   W9 (T2) capture: the ring sprite is PRESENT at the fixed draw position --
#      the capture differs from the island-only P6.5b1 model inside the
#      sprite rect but matches outside it (sprite drawn, nothing else broke).
#
# Usage:  python tools/_portspike/qa_p6_sprite.py [savestate.mcs] [capture.png] [map]
# Captures: same commands as qa_p6_vdp2.py (shared build + boot).
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
ITEMS_GIF = os.path.join(ROOT, "extracted", "Data", "Sprites", "Global", "Items.gif")
MODEL_PNG = os.path.join(HERE, "_p6", "_p6_vdp2_model.png")  # island-only reference

# Fixed draw position in SCREEN px (the present's contract; over the black
# left band of the island window for contrast). The proof draws anim 0
# ("Normal Ring") frames whose max size is 16x16 around this center.
DRAW_CX, DRAW_CY = 60, 112

SYMS = ["_p6_w_spr_id", "_p6_w_spr_animcount", "_p6_w_spr_f0xy", "_p6_w_spr_f0wh",
        "_p6_w_spr_f0pv", "_p6_w_spr_f0dur", "_p6_w_spr_sheethash",
        "_p6_w_spr_frame", "_p6_w_spr_ticks"]


def model():
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    from convert_ring_sprite import parse_spr
    sheets, anims = parse_spr(RING_BIN)
    a0 = anims[0]
    sheet_id, sx, sy, w, h, px, py, dur, uc = a0["frames"][0]
    from PIL import Image
    im = Image.open(ITEMS_GIF)
    raw = im.tobytes()
    hsh = 5381
    for b in raw:
        hsh = (((hsh << 5) + hsh) ^ b) & 0xFFFFFFFF
    return {
        "animcount": len(anims),
        "f0xy": ((sx << 16) | sy) & 0xFFFFFFFF,
        "f0wh": ((w << 16) | h) & 0xFFFFFFFF,
        "f0pv": (((px & 0xFFFF) << 16) | (py & 0xFFFF)) & 0xFFFFFFFF,
        "f0dur": dur,
        "sheethash": hsh,
        "speed": a0["speed"],
        "framecount": len(a0["frames"]),
        "sheetbytes": len(raw),
    }


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    cap = _scene._as_path(argv[2]) if len(argv) > 2 else None
    mp = _scene._as_path(argv[3]) if len(argv) > 3 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.5b2 SPRITE GATE: engine Ring animation set + VDP1 draw")
    print("=" * 72)
    m = model()
    print("  model: anims=%d f0 xy=0x%08X wh=0x%08X pv=0x%08X dur=%d sheet-hash=0x%08X"
          % (m["animcount"], m["f0xy"], m["f0wh"], m["f0pv"], m["f0dur"], m["sheethash"]))
    print("  savestate: %s" % mcs)
    print("  capture  : %s" % (cap or "(none -- T2 RED)"))
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
        print("        (Expected while the P6.5b2 body is unwritten.)")
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
                            signed=(s in ("_p6_w_spr_id", "_p6_w_spr_frame",
                                          "_p6_w_spr_ticks")))
         for s in SYMS}
    print("  peeked: " + "  ".join("%s=%s" % (s[8:], _scene._hx(v[s])) for s in SYMS))

    # W8 cadence: ProcessAnimation advances strictly on timer > duration
    # (Animation.cpp:166-175), so with equal durations frames advanced after
    # t calls = floor((t*speed - 1) / duration); frameID = advanced % count
    # (loopIndex 0). Witness order in the tick: ProcessAnimation, then copy
    # frameID, then ticks++ -- so ticks == number of ProcessAnimation calls.
    t = v["_p6_w_spr_ticks"] or 0
    exp_frame = (max(0, (t * m["speed"] - 1)) // m["f0dur"]) % m["framecount"] if t else 0

    checks = [
        # NOTE: id 0 is the VALID first slot -- `or -1` would coerce it to -1
        # (Python falsy zero), mis-reporting GREEN runs as RED.
        ("W1 LoadSpriteAnimation slot valid",
         v["_p6_w_spr_id"] is not None and v["_p6_w_spr_id"] >= 0,
         "id=%s" % v["_p6_w_spr_id"]),
        ("W2 animCount == %d" % m["animcount"],
         v["_p6_w_spr_animcount"] == m["animcount"],
         "got %s" % v["_p6_w_spr_animcount"]),
        ("W3 frame0 sprX/sprY byte-exact", (v["_p6_w_spr_f0xy"] or 0) & 0xFFFFFFFF == m["f0xy"],
         "got %s expect 0x%08X" % (_scene._hx(v["_p6_w_spr_f0xy"]), m["f0xy"])),
        ("W4 frame0 w/h byte-exact", (v["_p6_w_spr_f0wh"] or 0) & 0xFFFFFFFF == m["f0wh"],
         "got %s expect 0x%08X" % (_scene._hx(v["_p6_w_spr_f0wh"]), m["f0wh"])),
        ("W5 frame0 pivot byte-exact", (v["_p6_w_spr_f0pv"] or 0) & 0xFFFFFFFF == m["f0pv"],
         "got %s expect 0x%08X" % (_scene._hx(v["_p6_w_spr_f0pv"]), m["f0pv"])),
        ("W6 frame0 duration == %d" % m["f0dur"], v["_p6_w_spr_f0dur"] == m["f0dur"],
         "got %s" % v["_p6_w_spr_f0dur"]),
        ("W7 sheet pixels (%d B) byte-exact via hash" % m["sheetbytes"],
         (v["_p6_w_spr_sheethash"] or 0) & 0xFFFFFFFF == m["sheethash"],
         "got %s expect 0x%08X" % (_scene._hx(v["_p6_w_spr_sheethash"]), m["sheethash"])),
        ("W8 ProcessAnimation cadence (ticks=%s -> frame %d)"
         % (v["_p6_w_spr_ticks"], exp_frame),
         v["_p6_w_spr_ticks"] is not None and (v["_p6_w_spr_ticks"] or 0) > 8
         and v["_p6_w_spr_frame"] == exp_frame,
         "frame=%s" % v["_p6_w_spr_frame"]),
    ]

    # T2: sprite PRESENT at the fixed position -- capture differs from the
    # island-only model inside the sprite rect, matches outside (reuses the
    # island-anchored mapping from qa_p6_vdp2.py).
    if cap and os.path.isfile(cap):
        vd_spec = importlib.util.spec_from_file_location(
            "qa_visual_diff", os.path.join(ROOT, "tools", "qa_visual_diff.py"))
        vd = importlib.util.module_from_spec(vd_spec)
        vd_spec.loader.exec_module(vd)
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
            checks.append(("W9 sprite present", False, "island anchor not found"))
        else:
            # map capture into MODEL coordinates via the island anchor
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
            checks.append(("W9 ring present at (%d,%d): inside-diff>15%%, outside<10%%"
                           % (DRAW_CX, DRAW_CY),
                           inside > 0.15 and outside < 0.10,
                           "inside=%.1f%% outside=%.1f%%" % (100 * inside, 100 * outside)))
    else:
        checks.append(("W9 sprite present", False, "no capture provided"))

    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the engine loaded the ORIGINAL Ring animation set")
        print("        from Data.rsdk, ProcessAnimation is ticking it, and the")
        print("        Saturn backend draws the engine's current frame on VDP1")
        print("        over the engine-rendered layer. Engine objects are visible.")
        return 0
    print("RESULT: RED -- engine sprite path not proven (see witnesses).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
