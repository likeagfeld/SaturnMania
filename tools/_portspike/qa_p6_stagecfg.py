#!/usr/bin/env python3
# =============================================================================
# qa_p6_stagecfg.py -- P6.7c gate (Task #210): SCENE-DRIVEN CLASS REGISTRATION.
# The engine's OWN config chain -- LoadGameConfig (RetroEngine.cpp:1020-1195)
# -> LoadSceneFolder (Scene.cpp:28-286, incl LoadTileConfig :733 + StageConfig
# :166-270 + LoadStageGIF :980) -> LoadSceneAssets -> InitObjects
# (Object.cpp:327-362) -- runs on Saturn against the untouched 1.03 Data.rsdk,
# retiring the P6.7a hand-wiring (hand scene list, stageObjectIDs[1]=1,
# classCount=2, hand ENGINESTATE_REGULAR).
#
# EVERY expectation is derived LIVE each run from the original data files
# (the qa_p6_memmap pattern -- no baked constants to go stale):
#   C1 LoadGameConfig parsed the REAL GameConfig.bin: gameTitle == the
#      file's title string (djb2 witness). Saturn shim required: 1.03
#      GameConfig is REV01-format (NO per-scene filter byte; MEASURED
#      2026-06-10: REV01 parse consumes 4008/4008 bytes, REV02 desyncs at
#      the first scene entry) while the pack builds -DRETRO_REVISION=2.
#   C2 the ENGINE's hash loop (:1043-1060) matched "Ring" among the 46
#      GameConfig global-object names against the registered class list
#      [":DefaultObject:"@0 ":DevOutput:"@1 "Ring"@2] (the InitGameLink
#      preamble mirror, RetroEngine.cpp:1216-1235): globalObjectCount==3,
#      globalObjectIDs[2]==2.
#   C3 the engine-built scene list: categoryCount==8, listPos==0 (Logos =
#      cat[0].offsetStart + startScene), harness-discovered Title pos == 1.
#   C4 StageConfig path (Scene.cpp:179-206): Title useGlobalObjects==0 ->
#      classCount == TYPE_DEFAULT_COUNT == 2 after LoadSceneFolder; the
#      witnessed harness append (mirror of :199-205) brings it to 3.
#   C5 the Saturn LoadSfxToSlot alloc-guard skipped the global SFX that
#      exceed the 32 KB SFX pool (Audio.cpp:378 writes through the buffer
#      UNGUARDED upstream -- the engine's own bug note :360-362 cites the
#      class); skips inside the derived [floor..52] band AND the P6.6
#      proof SFX stayed loaded (S-gates re-run separately).
#   C6 LoadTileConfig (Scene.cpp:733-880 + Saturn flip-skip :883) parsed
#      Data/Stages/Title/TileConfig.bin BYTE-EXACTLY: djb2 over the SH-2
#      collisionMasks[2][1024] (64 B/tile) and tileInfo[2][1024] (5 B/tile)
#      windows equals this gate's independent Python replication of the
#      same algorithm against the same in-pack file.
#   C7 the CANONICAL palette chain (StageConfig stagePalette + GIF merge
#      into inactive rows only, Scene.cpp:988-998 + LoadSceneAssets
#      :308-319 active-row reload) produced fullPalette[0] == model
#      (djb2 over the 512 B big-endian uint16[256] SH-2 image).
#
# Usage: python tools/_portspike/qa_p6_stagecfg.py [savestate.mcs] [map]
# =============================================================================
import importlib.util
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

_rspec = importlib.util.spec_from_file_location(
    "rsdk_extract", os.path.join(ROOT, "tools", "rsdk_extract.py"))
_rsdk = importlib.util.module_from_spec(_rspec)
_rspec.loader.exec_module(_rsdk)

MCS_DEFAULT = os.path.join(HERE, "_p6", "_p6_pack.mcs")
GAMECONFIG = os.path.join(ROOT, "extracted", "Data", "Game", "GameConfig.bin")
STAGECONFIG = os.path.join(ROOT, "extracted", "Data", "Stages", "Title", "StageConfig.bin")
PACK_CANDIDATES = [os.path.join(ROOT, "cd", "DATA.RSDK"),
                   os.path.join(ROOT, "Data.rsdk")]

SYMS = ["_p6_w_cfg_titlehash", "_p6_w_cfg_globalcount", "_p6_w_cfg_ringgid",
        "_p6_w_cfg_catcount", "_p6_w_cfg_startpos", "_p6_w_cfg_titlepos",
        "_p6_w_cfg_classcount0", "_p6_w_cfg_classcount", "_p6_w_sfx_skips",
        "_p6_w_til_cmhash", "_p6_w_til_tihash", "_p6_w_pal_hash",
        "_p6_w_createslot"]

# MEASURED (P6.7c verification, allocator simulation against the real WAV
# sizes from the pack): with the two proof preloads first (ScoreAdd 5,876 B +
# MenuBleep 13,948 B F32 + two 16 B headers = 19,856 of 32,768), EXACTLY one
# of the 52 GameConfig global loads succeeds (the DUPLICATE ScoreAdd at list
# index 25 -- LoadSfx does not dedupe; pool ends 25,748 B) and 51 skip.
EXP_SFX_SKIPS = 51
# InitObjects (Object.cpp:330, TEMPENTITY_START substitution): RESERVE 0x40 +
# SCENE 0x440 at the W11b GHZ-scale entity flip (Object.hpp Saturn counts;
# TEMPENTITY_START = ENTITY_COUNT 0x500 - TEMP 0x80).
EXP_CREATESLOT = 0x480

# The classes the pack registers BEFORE LoadGameConfig, in objectClassList
# order: the InitGameLink mirror (DefaultObject/DevOutput), the overlay Ring,
# then the Player-wave game link (p6_wave1_reg.c, SonicMania_Game.c line
# order). Step B (Task #227): the dual-stride entity pool raised the
# RegisterObject refusal threshold to ENTITY_WIDE_SIZE (556), so Player/
# GameOver/ImageTrail now register -- the full 26-class set.
REGISTERED = [":DefaultObject:", ":DevOutput:", "Ring",
              "BoundsMarker", "Camera", "DebugMode", "DrawHelpers", "Dust",
              "ERZStart", "GameOver", "HUD", "Ice", "ImageTrail",
              "Localization", "LogHelpers", "MathHelpers", "Music",
              "Options", "PauseMenu", "Player", "SaveGame", "ScoreBonus",
              "Shield", "SizeLaser", "Soundboard", "Zone"]
TYPE_DEFAULT_COUNT = 2  # Object.hpp:135-137 (DEFAULTOBJECT=0, DEVOUTPUT)


def djb2(data):
    h = 5381
    for b in data:
        h = ((h << 5) + h) ^ b
        h &= 0xFFFFFFFF
    return h


def s32(v):
    return v - 0x100000000 if v >= 0x80000000 else v


# ---- Offline model: GameConfig.bin (REV01 / 1.03 layout, MEASURED) -----------
def read_string(f):
    n = f.read(1)[0]
    return f.read(n).rstrip(b"\x00").decode("latin1")


def parse_gameconfig(path):
    out = {}
    with open(path, "rb") as f:
        if f.read(4) != b"CFG\x00":
            raise ValueError("GameConfig signature")
        out["title"] = read_string(f)
        read_string(f)  # subtitle
        read_string(f)  # version
        out["activeCategory"] = f.read(1)[0]
        out["startScene"] = struct.unpack("<H", f.read(2))[0]
        objCnt = f.read(1)[0]
        out["objects"] = [read_string(f) for _ in range(objCnt)]
        pal = []
        for _b in range(8):
            rows = struct.unpack("<H", f.read(2))[0]
            colors = {}
            for r in range(0x10):
                if rows >> r & 1:
                    row = []
                    for _c in range(0x10):
                        red, green, blue = f.read(1)[0], f.read(1)[0], f.read(1)[0]
                        row.append((red, green, blue))
                    colors[r] = row
            pal.append((rows, colors))
        out["palette"] = pal
        sfxCnt = f.read(1)[0]
        out["sfx"] = []
        for _ in range(sfxCnt):
            p = read_string(f)
            f.read(1)
            out["sfx"].append(p)
        out["totalSceneCount"] = struct.unpack("<H", f.read(2))[0]
        catCount = f.read(1)[0]
        out["categories"] = []
        sid = 0
        for _c in range(catCount):
            cname = read_string(f)
            scnt = f.read(1)[0]
            scenes = []
            for _s in range(scnt):
                name, folder, sceneID = read_string(f), read_string(f), read_string(f)
                # 1.03 (REV01 layout): NO per-scene filter byte. MEASURED:
                # this parse consumes the file exactly end-to-end.
                scenes.append((name, folder, sceneID))
            out["categories"].append((cname, sid, scenes))
            sid += scnt
        varCount = f.read(1)[0]
        for _ in range(varCount):
            f.read(4)
            cnt = struct.unpack("<I", f.read(4))[0]
            f.read(4 * cnt)
        leftover = len(f.read())
    if leftover:
        raise ValueError("GameConfig model desync: %d bytes left" % leftover)
    return out


def parse_stageconfig(path):
    out = {}
    with open(path, "rb") as f:
        if f.read(4) != b"CFG\x00":
            raise ValueError("StageConfig signature")
        out["useGlobalObjects"] = f.read(1)[0]
        objCnt = f.read(1)[0]
        out["objects"] = [read_string(f) for _ in range(objCnt)]
        pal = []
        for _b in range(8):
            rows = struct.unpack("<H", f.read(2))[0]
            colors = {}
            for r in range(0x10):
                if rows >> r & 1:
                    row = []
                    for _c in range(0x10):
                        red, green, blue = f.read(1)[0], f.read(1)[0], f.read(1)[0]
                        row.append((red, green, blue))
                    colors[r] = row
            pal.append((rows, colors))
        out["palette"] = pal
        sfxCnt = f.read(1)[0]
        out["sfx"] = []
        for _ in range(sfxCnt):
            p = read_string(f)
            f.read(1)
            out["sfx"].append(p)
    return out


# ---- Offline model: in-pack file extraction (rsdk_extract API) ---------------
def pack_path():
    for p in PACK_CANDIDATES:
        if os.path.isfile(p):
            return p
    return None


_PACK_CACHE = {}


def pack_file(pack, relpath):
    """Extract one file from the datapack by its hash key."""
    if pack not in _PACK_CACHE:
        _PACK_CACHE[pack] = _rsdk.parse_datapack(pack)  # (raw, count, entries)
    raw, _count, entries = _PACK_CACHE[pack]
    key = _rsdk.lookup_hash(relpath)
    for e in entries:
        if e["hash"] == key:
            data = raw[e["offset"]:e["offset"] + e["size"]]
            if e["encrypted"]:
                data = _rsdk.decrypt(data, relpath, e["size"])
            return data
    return None


def read_compressed(blob, pos):
    """Reader.hpp:466-485: [u32 LE cSize+4][u32 BE uSize][zlib cSize bytes]."""
    cSize = struct.unpack_from("<I", blob, pos)[0] - 4
    uSize = struct.unpack_from(">I", blob, pos + 4)[0]
    raw = zlib.decompress(blob[pos + 8:pos + 8 + cSize])
    if len(raw) != uSize:
        raise ValueError("ReadCompressed model: %d != %d" % (len(raw), uSize))
    return raw, pos + 8 + cSize


# ---- Offline model: LoadTileConfig replication (Scene.cpp:733-880) -----------
def model_tileconfig(pack):
    blob = pack_file(pack, "Data/Stages/Title/TileConfig.bin")
    if blob is None:
        # MEASURED (2026-06-10): the 1.03 pack has NO Title TileConfig.bin
        # (GHZ's exists, 2,620 B). LoadTileConfig's LoadFile fails ->
        # returns at Scene.cpp:738 and never CLEARS the windows. P6.7 W11b:
        # the GHZ pass (p6_io_main step 3a-ghz) runs BEFORE the Title pass
        # and its LoadSceneFolder fills the packed window + tileInfo with
        # the GHZ TileConfig, so the 3d witnesses expect the GHZ-FILLED
        # hashes -- exactly the qa_p6_collision K3/K4 byte-exact contract
        # values (gen_collision_model.py).
        import json as _json
        cmod = _json.load(open(os.path.join(HERE, "_p6", "p6_collision_model.json")))
        return int(cmod["packed_hash"], 16), int(cmod["info_hash"], 16)
    if blob[:4] != b"TIL\x00":
        raise ValueError("TileConfig signature")
    raw, _ = read_compressed(blob, 4)
    TILE_SIZE = 16
    TILE_COUNT = 0x400
    cm = bytearray(2 * TILE_COUNT * 64)   # CollisionMask[2][1024]: 4x16 uint8
    ti = bytearray(2 * TILE_COUNT * 5)    # TileInfo[2][1024]: 5 uint8, no pad
    pos = 0
    for p in range(2):
        for t in range(TILE_COUNT):
            heights = raw[pos:pos + TILE_SIZE]; pos += TILE_SIZE
            active = raw[pos:pos + TILE_SIZE]; pos += TILE_SIZE
            yFlip = raw[pos]; pos += 1
            fA, lA, rA, roA, flag = raw[pos:pos + 5]; pos += 5
            tio = (p * TILE_COUNT + t) * 5
            ti[tio:tio + 5] = bytes((fA, lA, rA, roA, flag))
            floor = bytearray(16); roof = bytearray(16)
            lwall = bytearray(16); rwall = bytearray(16)
            if yFlip:
                for c in range(16):
                    if active[c]:
                        floor[c] = 0x00
                        roof[c] = heights[c]
                    else:
                        floor[c] = 0xFF
                        roof[c] = 0xFF
                for c in range(16):  # lWall: scan roofMasks upward, c <= m
                    h = 0
                    while True:
                        if h == 16:
                            lwall[c] = 0xFF
                            break
                        m = roof[h]
                        if m != 0xFF and c <= m:
                            lwall[c] = h
                            break
                        h += 1
                for c in range(16):  # rWall: scan roofMasks downward, c <= m
                    h = 15
                    while True:
                        if h == -1:
                            rwall[c] = 0xFF
                            break
                        m = roof[h]
                        if m != 0xFF and c <= m:
                            rwall[c] = h
                            break
                        h -= 1
            else:
                for c in range(16):
                    if active[c]:
                        floor[c] = heights[c]
                        roof[c] = 0x0F
                    else:
                        floor[c] = 0xFF
                        roof[c] = 0xFF
                for c in range(16):  # lWall: scan floorMasks upward, c >= m
                    h = 0
                    while True:
                        if h == 16:
                            lwall[c] = 0xFF
                            break
                        m = floor[h]
                        if m != 0xFF and c >= m:
                            lwall[c] = h
                            break
                        h += 1
                for c in range(16):  # rWall: scan floorMasks downward, c >= m
                    h = 15
                    while True:
                        if h == -1:
                            rwall[c] = 0xFF
                            break
                        m = floor[h]
                        if m != 0xFF and c >= m:
                            rwall[c] = h
                            break
                        h -= 1
            cmo = (p * TILE_COUNT + t) * 64
            cm[cmo:cmo + 16] = floor          # Scene.hpp:178 floorMasks
            cm[cmo + 16:cmo + 32] = lwall     # :179 lWallMasks
            cm[cmo + 32:cmo + 48] = rwall     # :180 rWallMasks
            cm[cmo + 48:cmo + 64] = roof      # :181 roofMasks
    if pos != len(raw):
        raise ValueError("TileConfig model desync: %d != %d" % (pos, len(raw)))
    return djb2(cm), djb2(ti)


# ---- Offline model: canonical fullPalette[0] (Scene.cpp:988-998 + :308-319) --
def gif_palette(pack):
    """Global color table of the in-pack Title 16x16Tiles.gif as 0xRRGGBB."""
    blob = pack_file(pack, "Data/Stages/Title/16x16Tiles.gif")
    if blob is None:
        return None
    if blob[:3] != b"GIF":
        raise ValueError("GIF signature")
    packed = blob[10]
    gct_size = 2 << (packed & 7)
    pal = []
    base = 13
    for i in range(min(gct_size, 256)):
        r, g, b = blob[base + 3 * i:base + 3 * i + 3]
        pal.append((r << 16) | (g << 8) | b)
    while len(pal) < 256:
        pal.append(0)
    return pal


def rgb565(red, green, blue):
    # p6_io_main.cpp:891-893 (Drawing.cpp:274-276 fill): R<<8 over 0xF8,
    # G<<3 over 0xFC, B>>3 -- RGB565.
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def model_fullpal0(pack, gc, sc):
    gif = gif_palette(pack)
    if gif is None:
        return None
    grows, gcolors = gc["palette"][0]
    srows, scolors = sc["palette"][0]
    pal = [0] * 256
    # LoadStageGIF (Scene.cpp:988-998): GIF colors land ONLY in rows inactive
    # in BOTH masks.
    for r in range(16):
        if not (srows >> r & 1) and not (grows >> r & 1):
            for c in range(16):
                v = gif[(r << 4) + c]
                pal[(r << 4) + c] = rgb565((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)
    # LoadSceneAssets:308-319: global rows first, then stage rows overwrite.
    for r in range(16):
        if grows >> r & 1:
            for c in range(16):
                red, green, blue = gcolors[r][c]
                pal[(r << 4) + c] = rgb565(red, green, blue)
        if srows >> r & 1:
            for c in range(16):
                red, green, blue = scolors[r][c]
                pal[(r << 4) + c] = rgb565(red, green, blue)
    return djb2(b"".join(struct.pack(">H", v) for v in pal))  # SH-2 BE image


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.7c STAGECFG GATE: engine GameConfig/StageConfig class registration")
    print("=" * 72)
    print("  savestate: %s" % mcs)
    print("-" * 72)

    # ---- live models -----------------------------------------------------
    gc = parse_gameconfig(GAMECONFIG)
    sc = parse_stageconfig(STAGECONFIG)

    exp_title = djb2(gc["title"].encode("latin1"))
    # engine hash loop simulation (RetroEngine.cpp:1043-1060)
    gids = [0, 1]
    for name in gc["objects"]:
        if name in REGISTERED:
            gids.append(REGISTERED.index(name))
    exp_globalcount = len(gids)
    exp_ringgid = gids[2] if len(gids) > 2 else -1
    exp_catcount = len(gc["categories"])
    exp_startpos = gc["categories"][gc["activeCategory"]][1] + gc["startScene"]
    exp_titlepos = None
    for cname, off, scenes in gc["categories"]:
        for i, (_n, folder, sid) in enumerate(scenes):
            if folder == "Title":
                exp_titlepos = off + i
                break
        if exp_titlepos is not None:
            break
    # Stage-class simulation (Scene.cpp stage loop): cc0 starts at the two
    # defaults, takes ALL globals when useGlobalObjects, then hash-matches
    # the StageConfig's own stage list against the registered classes
    # (Title: loadGlobalObjects=0 but its 11-entry list names Options +
    # Localization -- MEASURED -- so wave-1 raises cc0 2 -> 4).
    exp_cc0 = TYPE_DEFAULT_COUNT
    if sc["useGlobalObjects"]:
        exp_cc0 = exp_globalcount
    for name in sc["objects"]:
        if name in REGISTERED:
            exp_cc0 += 1
    exp_cc = exp_cc0 + 1  # harness Ring append (Scene.cpp:199-205 mirror)

    pk = pack_path()
    exp_cm = exp_ti = exp_pal = None
    if pk:
        exp_cm, exp_ti = model_tileconfig(pk)
        exp_pal = model_fullpal0(pk, gc, sc)

    print("  model: title djb2=0x%08X globals=%d ringgid=%s cats=%d" %
          (exp_title, exp_globalcount, exp_ringgid, exp_catcount))
    print("  model: startpos=%d titlepos=%s cc0=%d cc=%d sfx=%d" %
          (exp_startpos, exp_titlepos, exp_cc0, exp_cc, len(gc["sfx"])))
    print("  model: cmhash=%s tihash=%s palhash=%s (pack=%s)" %
          tuple([("0x%08X" % v) if v is not None else "n/a"
                 for v in (exp_cm, exp_ti, exp_pal)] + [pk]))
    print("-" * 72)

    # ---- witnesses --------------------------------------------------------
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
        print("        (Expected while the P6.7c body is unwritten.)")
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
    v = {s: _scene.peek_u32(mod, sections, syms[s], perm, signed=True)
         for s in SYMS}
    print("  peeked: " + "  ".join("%s=%s" % (s[8:], _scene._hx(v[s])) for s in SYMS))

    skips = v["_p6_w_sfx_skips"] or 0
    checks = [
        ("C1 LoadGameConfig parsed the REAL 1.03 GameConfig (title djb2)",
         (v["_p6_w_cfg_titlehash"] & 0xFFFFFFFF) == exp_title,
         "got %s expect 0x%08X" % (_scene._hx(v["_p6_w_cfg_titlehash"]), exp_title)),
        ("C2 engine hash-matched Ring: globalObjectCount==%d, gid[2]==%d" %
         (exp_globalcount, exp_ringgid),
         v["_p6_w_cfg_globalcount"] == exp_globalcount
         and v["_p6_w_cfg_ringgid"] == exp_ringgid,
         "count=%s gid=%s" % (v["_p6_w_cfg_globalcount"], v["_p6_w_cfg_ringgid"])),
        ("C3 scene list: cats==%d, engine listPos==%d, Title found at %s" %
         (exp_catcount, exp_startpos, exp_titlepos),
         v["_p6_w_cfg_catcount"] == exp_catcount
         and v["_p6_w_cfg_startpos"] == exp_startpos
         and v["_p6_w_cfg_titlepos"] == exp_titlepos,
         "cats=%s start=%s title=%s" % (v["_p6_w_cfg_catcount"],
                                        v["_p6_w_cfg_startpos"],
                                        v["_p6_w_cfg_titlepos"])),
        ("C4 StageConfig classCount: %d after LoadSceneFolder, %d after append" %
         (exp_cc0, exp_cc),
         v["_p6_w_cfg_classcount0"] == exp_cc0
         and v["_p6_w_cfg_classcount"] == exp_cc,
         "cc0=%s cc=%s" % (v["_p6_w_cfg_classcount0"], v["_p6_w_cfg_classcount"])),
        ("C5 SFX alloc-guard skips == %d exactly (32 KB pool vs %d globals; "
         "preload-first wiring, measured allocator sim)" %
         (EXP_SFX_SKIPS, len(gc["sfx"])),
         skips == EXP_SFX_SKIPS, "skips=%s" % skips),
        ("C8 createSlot == 0x%02X after InitObjects (TEMPENTITY_START; the "
         "uint16 wild-index 0xFFC0 class is retired)" % EXP_CREATESLOT,
         v["_p6_w_createslot"] == EXP_CREATESLOT,
         "got %s" % _scene._hx(v["_p6_w_createslot"])),
    ]
    if exp_cm is not None:
        checks.append(
            ("C6 LoadTileConfig byte-exact (cm 0x%08X / ti 0x%08X)" % (exp_cm, exp_ti),
             (v["_p6_w_til_cmhash"] & 0xFFFFFFFF) == exp_cm
             and (v["_p6_w_til_tihash"] & 0xFFFFFFFF) == exp_ti,
             "cm=%s ti=%s" % (_scene._hx(v["_p6_w_til_cmhash"]),
                              _scene._hx(v["_p6_w_til_tihash"]))))
    else:
        checks.append(("C6 (pack not present -- TileConfig model unavailable)",
                       False, "need cd/DATA.RSDK"))
    if exp_pal is not None:
        checks.append(
            ("C7 canonical fullPalette[0] (djb2 0x%08X)" % exp_pal,
             (v["_p6_w_pal_hash"] & 0xFFFFFFFF) == exp_pal,
             "got %s" % _scene._hx(v["_p6_w_pal_hash"])))

    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the engine's OWN GameConfig/StageConfig chain")
        print("        registered classes, built the scene list, parsed the")
        print("        TileConfig and produced the canonical palette -- all")
        print("        byte-checked against the original 1.03 data files.")
        return 0
    print("RESULT: RED -- scene-driven registration not proven (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
