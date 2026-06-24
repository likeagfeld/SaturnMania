#!/usr/bin/env python3
# =============================================================================
# _p67c_pal_model.py -- P6.7c: canonical fullPalette[0] model (LIVE per run).
#
# Models the engine's EXACT palette chain for the Title scene so the P6.7c
# gates derive their CRAM/pixel expectations from the same inputs the SH-2
# build consumes (no baked constants):
#
#   1. LoadGameConfig            RetroEngine.cpp:1062-1077
#        activeGlobalRows[b] = u16; active rows read 16 RGB triplets into
#        globalPalette[b][(r<<4)+c]; INACTIVE rows are ZEROED in globalPalette.
#   2. LoadSceneFolder           Scene.cpp:243-259
#        activeStageRows[b] = u16; same shape into stagePalette (inactive
#        rows zeroed in stagePalette).
#   3. LoadStageGIF              Scene.cpp:980-998 (called at Scene.cpp:272-273,
#        i.e. still inside LoadSceneFolder, BEFORE LoadSceneAssets)
#        For bank-0 rows INACTIVE in BOTH activeStageRows[0] AND
#        activeGlobalRows[0]: fullPalette[0][(r<<4)+c] = rgb565(GIF palette).
#        The GIF palette is read by ImageGIF::Load (Sprite.cpp:219-259):
#        GCT (palette_size = 1 << ((packed&7)+1)) into palette[0..size-1],
#        then IF the first image descriptor carries a local color table the
#        loop `c = 0x80; do { ++c; ... palette[c] } while (c != 0x100)`
#        writes the 128 local colors into palette[0x81..0x100] -- local[k]
#        lands at index 0x81+k (UPSTREAM off-by-one; the 128th write at
#        palette[0x100] is out of the 0x100-entry buffer and never reaches
#        fullPalette, so the model drops it).
#   4. LoadSceneAssets reload    Scene.cpp:308-319
#        per row r: activeGlobalRows bit -> fullPalette = globalPalette;
#        THEN activeStageRows bit -> fullPalette = stagePalette (stage wins).
#
#   rgb32To16 tables (Drawing.cpp:274-276 == p6_io_main.cpp:890-894):
#        R = (c & 0xF8) << 8 | G = (c & 0xFC) << 3 | B = c >> 3   (RGB565)
#
# Net effect per bank-0 row r:
#        stage row   if activeStageRows[0]  bit r
#   else global row  if activeGlobalRows[0] bit r
#   else GIF GCT row r
#
# API:
#   model_fullpal0(pack_path, gameconfig_path, stageconfig_path)
#       -> (bytes512, djb2)
#   bytes512 = big-endian uint16[256] (the SH-2 memory image of
#   RSDK::fullPalette[0]); djb2 = djb2-xor over those 512 bytes
#   (h = 5381; h = ((h<<5)+h) ^ byte; 32-bit), the same hash loop the pack
#   witnesses use (p6_io_main.cpp:901-903).
#
# Usage:
#   python tools/_portspike/_p6/_p67c_pal_model.py
#       [pack] [gameconfig] [stageconfig]
#   Defaults: cd/DATA.RSDK + extracted/Data/{Game/GameConfig.bin,
#   Stages/Title/StageConfig.bin}. __main__ prints the canonical hash, the
#   GIF-only (pre-P6.7c pack) baseline, the changed-entry list, and the
#   row analysis for the Ring sheet (Items.gif) + island CRAM witnesses.
# =============================================================================
import hashlib
import importlib.util
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))

PACK_DEFAULT = os.path.join(ROOT, "cd", "DATA.RSDK")
GCFG_DEFAULT = os.path.join(ROOT, "extracted", "Data", "Game", "GameConfig.bin")
SCFG_DEFAULT = os.path.join(ROOT, "extracted", "Data", "Stages", "Title",
                            "StageConfig.bin")
GIF_IN_PACK = "Data/Stages/Title/16x16Tiles.gif"

RSDK_SIGNATURE_CFG = 0x474643  # Scene.hpp:57


def _load_rsdk_extract():
    spec = importlib.util.spec_from_file_location(
        "rsdk_extract", os.path.join(ROOT, "tools", "rsdk_extract.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def pack_read(pack_path, inner_path):
    """Pull one file out of Data.rsdk exactly like LoadDataPack/OpenDataFile
    (Reader.cpp; offline port = tools/rsdk_extract.py, byte-verified P6.4)."""
    rx = _load_rsdk_extract()
    raw, _, entries = rx.parse_datapack(pack_path)
    h = rx.lookup_hash(inner_path)
    e = next((e for e in entries if e["hash"] == h), None)
    if e is None:
        raise KeyError("not in pack: %s" % inner_path)
    blob = raw[e["offset"]:e["offset"] + e["size"]]
    if e["encrypted"]:
        blob = rx.decrypt(blob, inner_path, e["size"])
    return blob


# ---- rgb32To16 (Drawing.cpp:274-276; c is uint8 so 0xFFF8==0xF8 masked) -----
def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


# ---- length-prefixed string (Reader.hpp:444-449) ----------------------------
class W:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def u8(self):
        v = self.d[self.p]; self.p += 1; return v

    def u16(self):
        v = struct.unpack_from("<H", self.d, self.p)[0]; self.p += 2; return v

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]; self.p += 4; return v

    def s(self):
        n = self.u8()
        v = self.d[self.p:self.p + n]; self.p += n
        return v.decode("latin-1")

    def take(self, n):
        v = self.d[self.p:self.p + n]; self.p += n; return v


def _read_palette_block(w):
    """The shared 8-bank palette block (RetroEngine.cpp:1062-1077 ==
    Scene.cpp:243-259): u16 activeRows per bank; each ACTIVE row = 16 RGB
    triplets; INACTIVE rows leave ZEROS in the target palette."""
    active = [0] * 8
    pal = [[0] * 256 for _ in range(8)]
    for b in range(8):
        active[b] = w.u16()
        for r in range(16):
            if (active[b] >> r) & 1:
                for c in range(16):
                    red, green, blue = w.u8(), w.u8(), w.u8()
                    pal[b][(r << 4) + c] = rgb565(red, green, blue)
    return active, pal


def parse_gameconfig(path):
    """RetroEngine.cpp:1020-1077 (REV02 read order)."""
    w = W(open(path, "rb").read())
    assert w.u32() == RSDK_SIGNATURE_CFG, "GameConfig: bad CFG signature"
    title, subtitle, version = w.s(), w.s(), w.s()
    w.u8()            # activeCategory
    w.u16()           # startScene
    objcnt = w.u8()
    for _ in range(objcnt):
        w.s()
    active, pal = _read_palette_block(w)
    return {"title": title, "subtitle": subtitle, "version": version,
            "objcnt": objcnt, "activeGlobalRows": active,
            "globalPalette": pal}


def parse_stageconfig(path):
    """Scene.cpp:171-259."""
    w = W(open(path, "rb").read())
    assert w.u32() == RSDK_SIGNATURE_CFG, "StageConfig: bad CFG signature"
    w.u8()            # useGlobalObjects
    objcnt = w.u8()
    for _ in range(objcnt):
        w.s()
    active, pal = _read_palette_block(w)
    return {"objcnt": objcnt, "activeStageRows": active, "stagePalette": pal}


def parse_gif_palette_engine(gif_bytes):
    """EXACT ImageGIF::Load palette walk (Sprite.cpp:202-259): GCT then the
    off-by-one local-color-table overlay at 0x81..0xFF ([0x100] dropped =
    engine OOB write past the 0x100-entry buffer)."""
    w = W(gif_bytes)
    w.take(6)                       # "GIF89a"
    w.u16(); w.u16()                # width, height (Seek_Set(6) + 2x u16)
    packed = w.u8()
    palette_size = 1 << ((packed & 7) + 1)
    w.take(2)                       # Seek_Cur(2): bg color idx + aspect
    pal = [0] * 256
    for c in range(palette_size):
        r, g, b = w.u8(), w.u8(), w.u8()
        pal[c] = (r << 16) | (g << 8) | b
    while w.u8() != 0x2C:           # byte-scan to ',' (Sprite.cpp:243-244)
        pass
    w.u16(); w.u16(); w.u16(); w.u16()
    data = w.u8()
    if (data >> 7) == 1:            # local color table (Sprite.cpp:252-258)
        for c in range(0x81, 0x101):
            r, g, b = w.u8(), w.u8(), w.u8()
            if c <= 0xFF:
                pal[c] = (r << 16) | (g << 8) | b
    return pal, palette_size, (data >> 7) == 1


def djb2_xor(data):
    h = 5381
    for byte in data:
        h = (((h << 5) + h) ^ byte) & 0xFFFFFFFF
    return h


def serialize_be(pal256):
    return struct.pack(">256H", *[v & 0xFFFF for v in pal256])


def compose_fullpal0(gif_pal, active_global0, global_pal0,
                     active_stage0, stage_pal0):
    """Scene.cpp:988-998 merge + Scene.cpp:308-319 reload, bank 0 net:
    stage row > global row > GIF GCT row."""
    full = [0] * 256
    for r in range(16):
        srow = (active_stage0 >> r) & 1
        grow = (active_global0 >> r) & 1
        for c in range(16):
            i = (r << 4) + c
            if not srow and not grow:
                p = gif_pal[i]
                full[i] = rgb565((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF)
        if grow:
            for c in range(16):
                full[(r << 4) + c] = global_pal0[(r << 4) + c]
        if srow:
            for c in range(16):
                full[(r << 4) + c] = stage_pal0[(r << 4) + c]
    return full


def gif_only_fullpal0(gif_pal):
    """The pre-P6.7c pack baseline (p6_io_main.cpp:858+898: LoadSceneAssets
    with both row masks ZERO, then LoadStageGIF writes ALL 16 rows)."""
    full = [0] * 256
    for i in range(256):
        p = gif_pal[i]
        full[i] = rgb565((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF)
    return full


def model_fullpal0(pack_path, gameconfig_path, stageconfig_path):
    """Canonical P6.7c fullPalette[0] -> (bytes512_BE, djb2_xor)."""
    gif_pal, _, _ = parse_gif_palette_engine(pack_read(pack_path, GIF_IN_PACK))
    g = parse_gameconfig(gameconfig_path)
    s = parse_stageconfig(stageconfig_path)
    full = compose_fullpal0(gif_pal,
                            g["activeGlobalRows"][0], g["globalPalette"][0],
                            s["activeStageRows"][0], s["stagePalette"][0])
    blob = serialize_be(full)
    return blob, djb2_xor(blob)


# ---- verification report ----------------------------------------------------
def _main(argv):
    pack = argv[1] if len(argv) > 1 else PACK_DEFAULT
    gcfg = argv[2] if len(argv) > 2 else GCFG_DEFAULT
    scfg = argv[3] if len(argv) > 3 else SCFG_DEFAULT

    gif_bytes = pack_read(pack, GIF_IN_PACK)
    loose = os.path.join(ROOT, "extracted", "Data", "Stages", "Title",
                         "16x16Tiles.gif")
    same = (os.path.isfile(loose)
            and hashlib.md5(open(loose, "rb").read()).hexdigest()
            == hashlib.md5(gif_bytes).hexdigest())
    print("in-pack GIF md5 == extracted/ copy: %s (%s)"
          % (same, hashlib.md5(gif_bytes).hexdigest()))

    gif_pal, gct_size, has_lct = parse_gif_palette_engine(gif_bytes)
    print("GIF GCT size=%d local-color-table=%s" % (gct_size, has_lct))

    g = parse_gameconfig(gcfg)
    s = parse_stageconfig(scfg)
    print("GameConfig: title=%r objcnt=%d" % (g["title"], g["objcnt"]))
    print("  activeGlobalRows = %s"
          % " ".join("0x%04X" % v for v in g["activeGlobalRows"]))
    print("StageConfig(Title): objcnt=%d" % s["objcnt"])
    print("  activeStageRows  = %s"
          % " ".join("0x%04X" % v for v in s["activeStageRows"]))

    full = compose_fullpal0(gif_pal,
                            g["activeGlobalRows"][0], g["globalPalette"][0],
                            s["activeStageRows"][0], s["stagePalette"][0])
    base = gif_only_fullpal0(gif_pal)
    blob, h = model_fullpal0(pack, gcfg, scfg)
    print("\nCANONICAL fullPalette[0] djb2-xor(BE uint16[256]) = 0x%08X" % h)
    print("GIF-only baseline      djb2-xor                   = 0x%08X"
          % djb2_xor(serialize_be(base)))

    changed = [i for i in range(256) if full[i] != base[i]]
    rows = sorted(set(i >> 4 for i in changed))
    print("changed entries vs GIF-only: %d of 256 (rows %s)"
          % (len(changed), rows))
    for i in changed:
        print("  idx %3d (row %2d): 0x%04X -> 0x%04X"
              % (i, i >> 4, base[i], full[i]))

    print("\nfinal 256 x RGB565 (canonical):")
    for r in range(16):
        print("  row %2d: %s" % (r, " ".join("%04X" % full[(r << 4) + c]
                                             for c in range(16))))
    return 0


if __name__ == "__main__":
    sys.exit(_main(sys.argv))
