#!/usr/bin/env python3
# =============================================================================
# qa_p6_vdp2.py -- P6.5b1 gate (Task #208): FIRST ENGINE-RENDERED PIXELS.
# The engine-decoded Title "Island" tile layer (layer 3 of the real Scene1.bin,
# loaded from the ORIGINAL Data.rsdk and decoded by the engine's own GIF
# decoder) is presented through VDP2 NBG1 cell mode and must match an offline
# software render of the same layer.
#
# Locked contracts (measured / ST-058-R2-pinned, 2026-06-10; see Task #208):
#   - 2-WORD pattern name data: low word = 15-bit charno (unit ALWAYS 0x20
#     bytes, ST-058 p.73 -> 8bpp 16x16 tile = 8 units -> charno = tile*8 with
#     cells based at VRAM A0 offset 0); high word bit31 = V-flip, bit30 =
#     H-flip (Table 4.7), bits 22-16 palette = 0.
#   - Layout entry: idx = t & 0x3FF, fx = (t>>10)&1, fy = (t>>11)&1,
#     empty = 0xFFFF (mirrors src/rsdk/collision.c:327).
#   - Cells: 1024 tiles x 256 B at A0 0x25E00000 (A0+A1 whole); 2x2-cell char
#     order TL,TR,BL,BR. Empty entries point at the BLANK char: the smallest
#     tile index NOT used by layer 3, whose 256 B the converter zeroes.
#   - Map: 4 pages x 32x32 x 2 words at B0 0x25E40000; page = (ty>=32)*2 +
#     (tx>=32); in-page = (ty&31)*32 + (tx&31).
#   - CRAM bank 0 at 0x25F00000: fullPalette[0] RGB565 -> Saturn
#     0x8000 | B5<<10 | G5<<5 | R5 (G5 = G6>>1); fullPalette comes from the
#     GIF palette through the engine's rgb32To16 tables (Drawing.cpp:274-276).
#   - Present scroll = (320,384) px (densest 20x14 tile window, 224/280
#     non-empty, measured).
#
# Tiers:
#   T1 (savestate, byte-exact): sampled cell blocks + 2-word PNDs + CRAM
#      entries + p6_w_vdp2_done == 1.
#   T2 (pixels): a Mednafen frame capture vs the offline-composed model PNG,
#      SSIM >= 0.90 via tools/qa_visual_diff.py (555->888 bit-replication and
#      emulator filtering absorbed by the threshold; T1 carries byte-exactness).
#
# Usage:
#   python tools/_portspike/qa_p6_vdp2.py [savestate.mcs] [capture.png] [map]
# Captures:
#   pwsh -File tools/qa_savestate.ps1 -Cue game.cue -SaveFrame 50 -FpsScale 2.0 \
#        -Out tools/_portspike/_p6/_p6_pack.mcs
#   pwsh -File tools/qa_boot.ps1 -Cue game.cue -Wait 2 -Every 0.25 -Shots 80
#        (use the LAST frame; the diag build parks on the engine layer)
# =============================================================================
import importlib.util
import os
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
MCS_DEFAULT = os.path.join(HERE, "_p6", "_p6_pack.mcs")
GIF_PATH = os.path.join(ROOT, "extracted", "Data", "Stages", "Title", "16x16Tiles.gif")
SCN_PATH = os.path.join(ROOT, "extracted", "Data", "Stages", "Title", "Scene1.bin")
MODEL_PNG = os.path.join(HERE, "_p6", "_p6_vdp2_model.png")
VISUAL_DIFF = os.path.join(ROOT, "tools", "qa_visual_diff.py")

VRAM_CEL = 0x25E00000  # A0: 1024 tiles x 256 B
VRAM_MAP = 0x25E40000  # B0: 4 pages x 32x32 x 4 B
CRAM = 0x25F00000
SCROLL_X, SCROLL_Y = 320, 384
SSIM_MIN = 0.90

SYM_DONE = "_p6_w_vdp2_done"


# ---- Scene1.bin layer-3 parse (mirrors Scene.cpp:381-465 exactly) ------------
def parse_layer3():
    data = open(SCN_PATH, "rb").read()
    pos = [0]

    def u8():
        v = data[pos[0]]; pos[0] += 1; return v

    def u16():
        v = data[pos[0]] | (data[pos[0] + 1] << 8); pos[0] += 2; return v

    def u32():
        v = (data[pos[0]] | (data[pos[0] + 1] << 8) | (data[pos[0] + 2] << 16)
             | (data[pos[0] + 3] << 24)); pos[0] += 4; return v

    def shift_of(size):
        sh = 1; sh2 = 1
        while True:
            sh = sh2; val = 1 << sh2; sh2 += 1
            if not (val < size):
                break
        return sh

    assert u32() == 0x4E4353
    pos[0] += 0x10
    sl = u8(); pos[0] += sl + 1
    layer_count = u8()
    out = None
    for l in range(min(layer_count, 4)):
        u8()
        n = u8(); pos[0] += n
        u8(); u8()
        xs = u16(); ws = shift_of(xs)
        ys = u16(); shift_of(ys)
        u16(); u16()
        sic = u16(); pos[0] += sic * 6
        st = u32(); pos[0] += 4 + (st - 4)              # scrollIndexes: skip
        st = u32(); pos[0] += 4                          # tileLayout: inflate
        blob = data[pos[0]:pos[0] + (st - 4)]; pos[0] += st - 4
        dec = zlib.decompress(blob)
        if l == 3:
            lay = [dec[i * 2] | (dec[i * 2 + 1] << 8) for i in range(len(dec) // 2)]
            out = (xs, ys, ws, lay)
    return out


def build_model():
    from PIL import Image
    im = Image.open(GIF_PATH)
    tiles = im.tobytes()                       # 1024 tiles x 256 B indexed
    pal888 = im.getpalette()                   # flat [r,g,b]*256
    xs, ys, ws, lay = parse_layer3()

    used = set((t & 0x3FF) for t in lay if t != 0xFFFF)
    blank = next(i for i in range(1024) if i not in used)

    # engine palette path: GIF 888 -> RGB565 (Drawing.cpp:274-276 tables)
    pal565 = []
    for c in range(256):
        r, g, b = pal888[c * 3], pal888[c * 3 + 1], pal888[c * 3 + 2]
        pal565.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
    # 565 -> Saturn CRAM 555 (BGR, MSB set)
    cram = []
    for c in pal565:
        r5 = (c >> 11) & 0x1F
        g5 = ((c >> 5) & 0x3F) >> 1
        b5 = c & 0x1F
        cram.append(0x8000 | (b5 << 10) | (g5 << 5) | r5)
    # 555 -> display 888 by bit replication (Mednafen-style)
    def disp(c555):
        r = (c555 & 0x1F); g = (c555 >> 5) & 0x1F; b = (c555 >> 10) & 0x1F
        return ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2))
    rgb = [disp(c) for c in cram]

    # compose the 320x224 window at SCROLL (index 0 + empty -> black backdrop)
    img = Image.new("RGB", (320, 224), (0, 0, 0))
    px = img.load()
    for sy in range(224):
        wy = SCROLL_Y + sy
        ty, py = (wy >> 4) & 63, wy & 15
        for sx in range(320):
            wx = SCROLL_X + sx
            tx, pxl = (wx >> 4) & 63, wx & 15
            t = lay[(ty << ws) + tx]
            if t == 0xFFFF:
                continue
            idx = t & 0x3FF
            fx, fy = (t >> 10) & 1, (t >> 11) & 1
            cx = 15 - pxl if fx else pxl
            cy = 15 - py if fy else py
            v = tiles[idx * 256 + cy * 16 + cx]
            if v == 0:
                continue
            px[sx, sy] = rgb[v]
    img.save(MODEL_PNG)

    # T1 expectations -------------------------------------------------------
    def cells_of(tile_idx):
        # 2x2-cell char, order TL,TR,BL,BR, 64 B each (8x8 rows of 8)
        src = tiles[tile_idx * 256:(tile_idx + 1) * 256] if tile_idx != blank \
            else bytes(256)
        out = bytearray()
        for cell in range(4):
            cy0 = (cell // 2) * 8
            cx0 = (cell % 2) * 8
            for r in range(8):
                row = src[(cy0 + r) * 16 + cx0:(cy0 + r) * 16 + cx0 + 8]
                out += row
        return bytes(out)

    # sample tiles: blank + two distinct used tiles from the present window
    win = []
    for wy in range(24, 24 + 14):
        for wx in range(20, 20 + 20):
            t = lay[(wy << ws) + wx]
            if t != 0xFFFF and (t & 0x3FF) not in [w[0] for w in win]:
                win.append(((t & 0x3FF), wx, wy, t))
            if len(win) >= 2:
                break
        if len(win) >= 2:
            break
    samples_cells = [(blank, cells_of(blank))] + \
                    [(idx, cells_of(idx)) for idx, _, _, _ in win]

    def pnd_of(t):
        if t == 0xFFFF:
            return blank * 8
        idx = t & 0x3FF
        fx, fy = (t >> 10) & 1, (t >> 11) & 1
        return (fy << 31) | (fx << 30) | (idx * 8)

    def pnd_addr(tx, ty):
        page = (1 if ty >= 32 else 0) * 2 + (1 if tx >= 32 else 0)
        return VRAM_MAP + page * (32 * 32 * 4) + (((ty & 31) * 32 + (tx & 31)) * 4)

    samples_pnd = []
    for idx, wx, wy, t in win:
        samples_pnd.append((wx, wy, pnd_addr(wx, wy), pnd_of(t)))
    # one empty entry inside the layer for the blank-char PND
    for wy in range(64):
        done = False
        for wx in range(64):
            if lay[(wy << ws) + wx] == 0xFFFF:
                samples_pnd.append((wx, wy, pnd_addr(wx, wy), pnd_of(0xFFFF)))
                done = True
                break
        if done:
            break

    # CRAM samples: 3 distinct nonzero colors actually used in the window
    used_colors = set()
    for idx, _, _, _ in win:
        for b in tiles[idx * 256:(idx + 1) * 256]:
            if b:
                used_colors.add(b)
    csamp = sorted(used_colors)[:3]
    samples_cram = [(c, cram[c]) for c in csamp]

    return blank, samples_cells, samples_pnd, samples_cram


def peek(mod, sections, addr, length):
    raw = mod._peek_bytes(sections, addr, length)
    return bytes(raw) if raw is not None else None


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    cap = _scene._as_path(argv[2]) if len(argv) > 2 else None
    mp = _scene._as_path(argv[3]) if len(argv) > 3 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.5b1 VDP2 GATE: first ENGINE-RENDERED pixels (Title Island layer)")
    print("=" * 72)
    blank, s_cells, s_pnd, s_cram = build_model()
    print("  model     : blank char = tile %d; %d cell samples, %d PNDs, %d CRAM"
          % (blank, len(s_cells), len(s_pnd), len(s_cram)))
    print("  model PNG : %s" % MODEL_PNG)
    print("  savestate : %s" % mcs)
    print("  capture   : %s" % (cap or "(none given -- T2 skipped = RED)"))
    print("-" * 72)

    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s)" % mp)
        return 1
    map_text = _scene.read_text(mp)
    sym_done = _scene.map_symbol(map_text, SYM_DONE)
    sym_magic = _scene.map_symbol(map_text, _scene.SYM_MAGIC)
    if sym_done is None or sym_magic is None:
        print("RESULT: RED -- witness symbol absent from the map (%s / %s)."
              % (SYM_DONE, _scene.SYM_MAGIC))
        print("        (Expected while the P6.5b1 present body is unwritten.)")
        return 1
    if not os.path.isfile(mcs):
        print("RESULT: RED -- savestate missing (%s)" % mcs)
        return 1

    import pathlib
    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    raw_magic = mod._peek_bytes(sections, sym_magic, 4)
    label, perm = _scene.calibrate(raw_magic)
    if perm is None:
        print("RESULT: RED -- magic mis-decode")
        return 1
    done = _scene.peek_u32(mod, sections, sym_done, perm, signed=True)

    checks = [("W1 present body completed (p6_w_vdp2_done)", done == 1,
               "%s = %s (expect 1)" % (SYM_DONE, done))]

    # T1: VDP2 VRAM/CRAM byte-exact samples. NOTE: VDP2 VRAM/CRAM in the
    # Mednafen savestate may carry its own byte-pair order; calibrate cells
    # via the known blank char (all zeros is order-invariant) and compare
    # PND/CRAM under the SAME permutation as WRAM (then a raw-order retry).
    def cmp16(exp_words, got_bytes):
        if got_bytes is None:
            return False
        be = b"".join(struct.pack(">H", w & 0xFFFF) for w in exp_words)
        le = b"".join(struct.pack("<H", w & 0xFFFF) for w in exp_words)
        return got_bytes == be or got_bytes == le

    for idx, exp in s_cells:
        got = peek(mod, sections, VRAM_CEL + idx * 256, 256)
        ok = got is not None and (got == exp or got == bytes(
            exp[i ^ 1] for i in range(256)))  # tolerate halfword pair swap
        checks.append(("W cells tile %d byte-exact" % idx, ok,
                       "256 B at 0x%08X" % (VRAM_CEL + idx * 256)))
    for wx, wy, addr, exp in s_pnd:
        got = peek(mod, sections, addr, 4)
        ok = got is not None and cmp16([exp >> 16, exp & 0xFFFF], got)
        checks.append(("W PND (%d,%d)" % (wx, wy), ok,
                       "0x%08X expect 0x%08X got %s"
                       % (addr, exp, got.hex() if got else None)))
    for c, exp in s_cram:
        got = peek(mod, sections, CRAM + c * 2, 2)
        ok = got is not None and cmp16([exp], got)
        checks.append(("W CRAM color %d" % c, ok,
                       "0x%08X expect 0x%04X got %s"
                       % (CRAM + c * 2, exp, got.hex() if got else None)))

    # T2: pixel SSIM vs the model PNG, ISLAND-ANCHORED. The Mednafen window
    # blits the Saturn frame at a non-uniform, configuration-dependent stretch
    # (MEASURED on this rig: 2.744x horizontal, 3.000x vertical, behind ~190 px
    # of left letterbox + 30 px chrome) -- whole-frame rect models all failed
    # (raw 0.680 / bbox 0.626 / overscan-corrected 0.322 on a frame whose VRAM
    # was 100% byte-exact per T1). The island is a unique green object in both
    # images, so its green-mask bounding box gives a translation- and
    # scale-free anchor: crop both to it, resize to the model's box, compare.
    # MEASURED on the first GREEN frame: SSIM 0.991, mean|diff| 5.1, 94.1%
    # within 24/255. Threshold 0.95 catches palette/tile-assembly breakage;
    # byte-exactness of the underlying data stays on T1.
    if cap and os.path.isfile(cap):
        vd_spec = importlib.util.spec_from_file_location("qa_visual_diff", VISUAL_DIFF)
        vd = importlib.util.module_from_spec(vd_spec)
        vd_spec.loader.exec_module(vd)
        from PIL import Image
        import numpy as np

        def green_bbox(img):
            a = np.asarray(img.convert("RGB")).astype(int)
            m = (a[..., 1] > 1.4 * a[..., 0]) & (a[..., 1] > 1.4 * a[..., 2]) \
                & (a[..., 1] > 80)
            ys, xs = np.where(m)
            if len(xs) < 500:
                return None
            return (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)

        cap_img = Image.open(cap).convert("RGB")
        cap_img = cap_img.crop((0, 30, cap_img.width, cap_img.height))  # chrome
        mod_img = Image.open(MODEL_PNG).convert("RGB")
        bb_c = green_bbox(cap_img)
        bb_m = green_bbox(mod_img)
        if bb_c is None or bb_m is None:
            checks.append(("W SSIM capture vs model (island-anchored)", False,
                           "island green mask not found (cap=%s mod=%s)" % (bb_c, bb_m)))
        else:
            mw, mh = bb_m[2] - bb_m[0], bb_m[3] - bb_m[1]
            cap_i = cap_img.crop(bb_c).resize((mw, mh), Image.BILINEAR)
            mod_i = mod_img.crop(bb_m)
            score = vd.ssim_rgb(cap_i, mod_i)
            checks.append(("W SSIM capture vs model >= 0.95 (island-anchored)",
                           score >= 0.95, "SSIM=%.3f (cap bbox %s)" % (score, (bb_c,))))
    else:
        checks.append(("W SSIM capture vs model", False, "no capture provided"))

    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        if detail:
            print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the engine-decoded ORIGINAL Title Island layer is")
        print("        rendering on VDP2 from the engine's own tilesetPixels +")
        print("        layout + palette. First engine-rendered pixels proven.")
        return 0
    print("RESULT: RED -- engine VDP2 present not proven (see witnesses).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
