#!/usr/bin/env python3
# =============================================================================
# qa_p6_gif.py -- P6.5a gate (Task #208): the UNMODIFIED engine decodes the
# REAL Title tileset GIF out of the ORIGINAL Data.rsdk into tilesetPixels.
#
# WHAT IT PROVES (GREEN):
#   1. RSDK::LoadStageGIF("Data/Stages/Title/16x16Tiles.gif") ran on SH-2:
#      the path resolved BY MD5 HASH inside the mounted pack, ImageGIF::Load
#      (the engine's own LZW GIF decoder, Sprite.cpp:202-267) header-opened it
#      (16 x 16384 == TILE_SIZE x TILE_COUNT*TILE_SIZE, the LoadStageGIF guard
#      at Scene.cpp:984), and the second Load(NULL, false) decoded ALL
#      262,144 indexed pixels straight into the WRAM-L tilesetPixels backing.
#   2. The pixels are BYTE-EXACT: p6_w_gif_hash is a djb2-xor over the whole
#      0x40000-byte tilesetPixels computed ON the SH-2 after the decode, and
#      must equal the model THIS gate derives at runtime by decoding the same
#      GIF from extracted/Data with Pillow (no hard-coded magic -- if the
#      asset changes, the model follows). One flipped byte anywhere fails it.
#   3. p6_w_gif_b0 cross-checks the first tile byte (offline 0x01) so a
#      zero-hash-vs-zero-buffer coincidence cannot false-green.
#
# RED fires when: witness symbols absent from game.map (current state -- the
# P6.5a body is unwritten), capture missing, magic mis-decode, PC outside the
# loaded image, or any value mismatch.
#
# Machinery imported from the proven P6.3 gate (one source of truth).
# Usage:  python tools/_portspike/qa_p6_gif.py [savestate.mcs] [link.map]
# Capture: pwsh -File tools/qa_savestate.ps1 -Cue game.cue -SaveFrame 50 \
#              -FpsScale 2.0 -Out tools/_portspike/_p6/_p6_pack.mcs
#          (shared capture with qa_p6_pack.py -- same build, same boot)
# =============================================================================
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

MCS_DEFAULT = os.path.join(HERE, "_p6", "_p6_pack.mcs")
GIF_PATH = os.path.normpath(os.path.join(
    HERE, "..", "..", "extracted", "Data", "Stages", "Title", "16x16Tiles.gif"))
TILESET_SIZE = 0x40000

SYM_GIF_LOADED = "_p6_w_gif_loaded"
SYM_GIF_HASH   = "_p6_w_gif_hash"
SYM_GIF_B0     = "_p6_w_gif_b0"
GIF_SYMS = [SYM_GIF_LOADED, SYM_GIF_HASH, SYM_GIF_B0]


def offline_model():
    from PIL import Image
    im = Image.open(GIF_PATH)
    if im.mode != "P":
        raise SystemExit("model error: %s is not palette-mode" % GIF_PATH)
    raw = im.tobytes()
    buf = bytearray(TILESET_SIZE)
    buf[: len(raw)] = raw  # engine decode covers width*height; memset-0 covers any tail
    h = 5381
    for b in buf:
        h = (((h << 5) + h) ^ b) & 0xFFFFFFFF
    return im.size, h, buf[0]


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.5a GIF GATE: engine decodes the REAL tileset GIF from Data.rsdk")
    print("=" * 72)
    if not os.path.isfile(GIF_PATH):
        print("RESULT: RED -- offline model source missing (%s)" % GIF_PATH)
        return 1
    (w, hgt), exp_hash, exp_b0 = offline_model()
    print("  model     : %dx%d -> hash 0x%08X  b0 0x%02X   (Pillow decode of the same GIF)"
          % (w, hgt, exp_hash, exp_b0))
    print("  savestate : %s" % mcs)
    print("  link map  : %s" % mp)
    print("-" * 72)

    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s). Build `make P6SCENE=1` first." % mp)
        return 1
    map_text = _scene.read_text(mp)

    text_lo = _scene.map_symbol(map_text, _scene.SYM_TEXT_LO) \
        or _scene.map_symbol(map_text, _scene.SYM_TEXT_LO_FB)
    text_hi = _scene.map_symbol(map_text, _scene.SYM_TEXT_HI) \
        or _scene.map_symbol(map_text, _scene.SYM_TEXT_HI_FB)

    syms = {}
    missing = []
    if text_lo is None or text_hi is None:
        missing.append("text bounds")
    for s in [_scene.SYM_MAGIC] + GIF_SYMS:
        syms[s] = _scene.map_symbol(map_text, s)
        if syms[s] is None:
            missing.append(s)

    print("[1/3] Witness symbols resolved from the link map:")
    for s in [_scene.SYM_MAGIC] + GIF_SYMS:
        print("        %-22s %s" % (s, ("0x%08X" % syms[s]) if syms[s] is not None else "ABSENT"))
    if missing:
        print("-" * 72)
        print("RESULT: RED -- witness symbol(s) absent from the map:")
        for s in missing:
            print("        %s" % s)
        print("        (Expected while the P6.5a LoadStageGIF body is unwritten.)")
        return 1

    if not os.path.isfile(mcs):
        print("-" * 72)
        print("RESULT: RED -- savestate missing (%s); capture per the header." % mcs)
        return 1

    import pathlib
    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))

    raw_magic = mod._peek_bytes(sections, syms[_scene.SYM_MAGIC], 4)
    label, perm = _scene.calibrate(raw_magic)
    if perm is None:
        print("RESULT: RED -- magic mis-decode (raw=%s)"
              % (raw_magic.hex() if raw_magic else "None"))
        return 1
    print("[2/3] byte-order calibration: %s" % label)

    regs = mod._sh2_regs(sections, "master")
    pc = regs.get("PC") if regs else None

    vals = {s: _scene.peek_u32(mod, sections, syms[s], perm,
                               signed=(s in (SYM_GIF_LOADED, SYM_GIF_B0)))
            for s in GIF_SYMS}

    print("[3/3] P6.5a witnesses:")
    checks = [
        ("W0 SH2-M PC in loaded image [0x%08X,0x%08X)" % (text_lo, text_hi),
         text_lo <= pc < text_hi, "PC=0x%08X" % pc),
        ("W1 LoadStageGIF ran to completion (decode + hash done)",
         vals[SYM_GIF_LOADED] == 1,
         "%s = %s (expect 1)" % (SYM_GIF_LOADED, vals[SYM_GIF_LOADED])),
        ("W2 tilesetPixels[0x40000] BYTE-EXACT vs the Pillow model",
         (vals[SYM_GIF_HASH] or 0) & 0xFFFFFFFF == exp_hash,
         "%s = 0x%08X (expect 0x%08X)"
         % (SYM_GIF_HASH, (vals[SYM_GIF_HASH] or 0) & 0xFFFFFFFF, exp_hash)),
        ("W3 first tile byte cross-check",
         vals[SYM_GIF_B0] == exp_b0,
         "%s = %s (expect 0x%02X)" % (SYM_GIF_B0, vals[SYM_GIF_B0], exp_b0)),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the engine's own GIF decoder pulled the ORIGINAL")
        print("        16x16Tiles.gif out of Data.rsdk by hash and produced the")
        print("        byte-exact 256 KB tileset on SH-2. Engine-side asset")
        print("        decode is proven; P6.5b (VDP1/VDP2 draw) is unblocked.")
        return 0
    print("RESULT: RED -- engine GIF decode not proven (see witnesses).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
