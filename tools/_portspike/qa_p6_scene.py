#!/usr/bin/env python3
# P6.3 gate (Task #207) -- prove the UNMODIFIED engine LoadScene chain parses a
# REAL on-disc Scene.bin on SH-2 and lands every entity's position in
# objectEntityList[] at byte-exact offsets, over the proven P6.2 GFS backend.
#
# WHY THIS IS THE P6.3 MILESTONE (engine scene parse over CD/GFS)
#   P6.1 (#205) proved the engine's OWN dispatch table runs on SH-2; P6.2 (#206)
#   proved the engine's UNMODIFIED RSDK::LoadFile reads a real CD file through a
#   Saturn FileIO backend (p6_gfs.c -> SGL GFS). P6.3 is the next link in the real
#   boot: RetroEngine ENGINESTATE_LOAD -> LoadSceneFolder -> LoadSceneAssets
#   (Scene.cpp:283). LoadSceneAssets opens "Data/Stages/Title/Scene1.bin" via that
#   SAME LoadFile path, walks the object table (Scene.cpp:455-554) and the per-var
#   type stream (559-673), and writes each entity's position into
#   objectEntityList[slotID + RESERVE_ENTITY_COUNT].position (Scene.cpp:535-554).
#   GREEN proves the engine's real scene parser ran end-to-end on SH-2 and placed
#   bytes that match what an offline parser extracts from the same Scene1.bin.
#
# ZERO-REGISTERED-CLASS PARSE (the measured, byte-exact basis of this gate)
#   This proof links the engine scene/object/storage code but registers NO object
#   classes (sceneInfo.classCount stays 0). LoadSceneAssets reads each var's TYPE
#   from the file (Scene.cpp:531) and sizes every read by that type, so the entity
#   byte-stream stays synced regardless of classCount: the hash-match loop (481)
#   never runs -> classID=0; stageObjectIDs[0] is accessed address-only at
#   classID=0 (if(classID) false -> no serialize()); EVERY var type
#   (uint8/16/32, enum, bool, string [u16 len + len*2 bytes], vector2 [2x i32],
#   color, float) advances the cursor correctly via the inactive/Seek_Cur branch.
#   So position.x/y is parsed byte-exact for EVERY entity, including the ones
#   AFTER the string-bearing Music object -- which is why the three witnesses below
#   span pre- and post-Music objects (proving end-to-end stream sync, not just the
#   first record).
#
# THE THREE ENTITY WITNESSES (offline-parsed from the real Title/Scene1.bin via
# tools/parse_title_entities.py; position stored RAW = on-disc fixed-point pixels
# << 16, i.e. ReadInt32(...,false) keeps the disk value verbatim):
#   TitleSonic    slot  8 -> objectEntityList[ 8+0x40] x=0x00FC0000 (252px)  y=0x00680000 (104px)
#   TitleLogo     slot  5 -> objectEntityList[ 5+0x40] x=0x01000000 (256px)  y=0x006C0000 (108px)  (EMBLEM)
#   Title3DSprite slot 16 -> objectEntityList[16+0x40] x=0x02200000 (544px)  y=0x01BC0000 (444px)
#
# THE RUNTIME WITNESSES (C-linkage globals in p6_io_main.o(.bss), WRAM-H, read
# from a Mednafen savestate; same byte order as _p6_w_magic, the shared anchor)
#   REQUIRED (the gate hard-fails on any):
#     p6_w_magic          const 0x12345678 -- WRAM byte-order self-calibration.
#     p6_w_scene_loaded   1  <- p6_scene_run reached the post-LoadSceneAssets
#                         witness copy (LoadSceneAssets is void; "loaded" means the
#                         chain returned without faulting). RED build: 0.
#     p6_w_scene_sonic_x/y   objectEntityList[ 8+0x40].position.x/y  (TitleSonic)
#     p6_w_scene_emblem_x/y  objectEntityList[ 5+0x40].position.x/y  (TitleLogo)
#     p6_w_scene_t3d_x/y     objectEntityList[16+0x40].position.x/y  (Title3DSprite)
#   DIAGNOSTIC (printed, NOT gated -- self-diagnose a failed GREEN):
#     p6_w_scene_step        boot progress: 1 InitStorage true, 2 scene globals
#                            set, 3 LoadSceneAssets returned, 4 witnesses copied.
#     p6_w_scene_initstorage InitStorage() return (1 == all engine pools malloc'd).
#   (objectCount is a LOCAL at Scene.cpp:468 -- it never reaches a global, so it
#    cannot be witnessed without modifying the unmodified engine; the offline
#    parse confirms 12 object classes / 76 entities / 2589 bytes consumed, and the
#    three cross-stream coord witnesses below already prove the parse stayed synced
#    across the whole table, so no objectCount runtime witness is needed.)
#
# RED vs GREEN (one gate, two builds -- the RED->GREEN demonstration)
#   RED   : the current tree has NO P6SCENE build (the scene witnesses are not in
#           any linked map and there is no scene savestate), so the gate fires RED
#           ("artifacts missing" / "witness symbol(s) absent"). It also fires RED
#           if the magic anchor mis-reads (image did not load / wrong bank), if the
#           SH-2 PC is outside core .text (crash), or if any coordinate disagrees
#           with the offline-parsed model.
#   GREEN : the P6SCENE=1 build links the engine LoadScene closure + p6_io_main.o
#           (-DP6_SCENE_TEST), jo boots (slInitSystem/CDC_CdInit/GFS_Init) and
#           calls p6_scene_run() -> InitStorage()+LoadSceneAssets("Title"/id="1").
#           All six coords match + loaded==1 -> the gate ACCEPTS it.
#
#   python tools/_portspike/qa_p6_scene.py [savestate.mcs] [link.map]
#   python tools/_portspike/qa_p6_scene.py --selftest    # prove the RED path fires
#
# P6 STATUS: P6.3 scaffolding (Saturn-only, build-gated; shipping ISO untouched
# unless P6SCENE=1). GREEN here is the "engine scene parser runs on SH-2"
# checkpoint -- STOP and report; P6.4-P6.7 (RenderDevice/AudioDevice/decomp object
# set/hand-port retirement) proceed only under user direction.

import importlib.util
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# Path A (P6SCENE=1) links into the project's full jo image, whose map is the
# repo-root game.map. The scene savestate is captured into _p6/_p6_scene.mcs.
MAP_DEFAULT = os.path.normpath(os.path.join(HERE, "..", "..", "game.map"))
MCS_DEFAULT = os.path.join(HERE, "_p6", "_p6_scene.mcs")
MCS_EXTRACT = os.path.normpath(os.path.join(HERE, "..", "mcs_extract.py"))

TEXT_LOAD = 0x06004000
WRAM_H = (0x06000000, 0x06100000)
WRAM_L = (0x00200000, 0x00300000)

# ---- Deterministic expected witness values (offline-parsed from the real
#      Title/Scene1.bin; position = on-disc fixed-point pixels << 16) -----------
EXP_LOADED   = 1
EXP_SONIC_X  = 0x00FC0000   # 252 px (TitleSonic   slot 8)
EXP_SONIC_Y  = 0x00680000   # 104 px
EXP_EMBLEM_X = 0x01000000   # 256 px (TitleLogo    slot 5, EMBLEM)
EXP_EMBLEM_Y = 0x006C0000   # 108 px
EXP_T3D_X    = 0x02200000   # 544 px (Title3DSprite slot 16)
EXP_T3D_Y    = 0x01BC0000   # 444 px

# Witness symbol names (sh-none-elf one-underscore convention; map_symbol tolerant)
SYM_TEXT_LO    = "__text_start"
SYM_TEXT_HI    = "__text_end"
# Path-A (full-jo build via COMMON/sgl.linker, which must NOT be modified --
# CLAUDE.md 10) defines neither __text_start nor __text_end. It DOES define
# ___Start (SLSTART @0x06004000 == .text load base) and __bstart (start of the
# .bss output section == end of ALL loaded image bytes). __etext is NOT a valid
# upper bound here -- MEASURED on the first P6SCENE capture (2026-06-09):
# sgl.linker's .text collects only literal *(.text), so SGL archive code lands
# ABOVE it (slSynch @0x06039170 vs __etext 0x060228D4; the healthy vblank-parked
# title loop sits inside slSynch at every capture), as do the -ffunction-sections
# orphan .text.* sections of the P6 pack (p6_scene_run @0x06024C50). [___Start,
# __bstart) brackets the whole loaded image; W0 keeps its meaning -- "PC is in
# the loaded program, not crashed to the BIOS spin handler 0x06000956 (which
# lies BELOW ___Start)".
SYM_TEXT_LO_FB = "___Start"
SYM_TEXT_HI_FB = "__bstart"
SYM_MAGIC      = "_p6_w_magic"
SYM_LOADED     = "_p6_w_scene_loaded"
SYM_SONIC_X    = "_p6_w_scene_sonic_x"
SYM_SONIC_Y    = "_p6_w_scene_sonic_y"
SYM_EMBLEM_X   = "_p6_w_scene_emblem_x"
SYM_EMBLEM_Y   = "_p6_w_scene_emblem_y"
SYM_T3D_X      = "_p6_w_scene_t3d_x"
SYM_T3D_Y      = "_p6_w_scene_t3d_y"
SYM_STEP       = "_p6_w_scene_step"
SYM_INITSTOR   = "_p6_w_scene_initstorage"

COORD_SYMS = [SYM_SONIC_X, SYM_SONIC_Y, SYM_EMBLEM_X, SYM_EMBLEM_Y,
              SYM_T3D_X, SYM_T3D_Y]
REQUIRED_SYMS   = [SYM_MAGIC, SYM_LOADED] + COORD_SYMS
DIAGNOSTIC_SYMS = [SYM_STEP, SYM_INITSTOR]
WITNESS_SYMS    = [SYM_LOADED] + COORD_SYMS + DIAGNOSTIC_SYMS

EXP_COORD = {
    SYM_SONIC_X: EXP_SONIC_X, SYM_SONIC_Y: EXP_SONIC_Y,
    SYM_EMBLEM_X: EXP_EMBLEM_X, SYM_EMBLEM_Y: EXP_EMBLEM_Y,
    SYM_T3D_X: EXP_T3D_X, SYM_T3D_Y: EXP_T3D_Y,
}
COORD_LABEL = {
    SYM_SONIC_X: "TitleSonic.x   (slot 8)",
    SYM_SONIC_Y: "TitleSonic.y   (slot 8)",
    SYM_EMBLEM_X: "TitleLogo.x    (slot 5)",
    SYM_EMBLEM_Y: "TitleLogo.y    (slot 5)",
    SYM_T3D_X: "Title3DSprite.x (slot 16)",
    SYM_T3D_Y: "Title3DSprite.y (slot 16)",
}

MAGIC_VALUE = 0x12345678
MAGIC_BE = bytes([0x12, 0x34, 0x56, 0x78])
PERMS = [
    ("big-endian (identity)", (0, 1, 2, 3)),
    ("16-bit pair-swap", (1, 0, 3, 2)),
    ("full little-endian", (3, 2, 1, 0)),
    ("32-bit word-swap", (2, 3, 0, 1)),
]


def read_text(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def map_symbol(map_text, name):
    """Address of a defined symbol from GNU ld -Map output, tolerating the
    sh-none-elf one-underscore strip on object symbols."""
    bare = name[1:] if name.startswith("_") else name
    # Tolerate a C++ namespace prefix (e.g. `RSDK::p6_saturn_anim_allocfail`): the
    # anim-pool diagnostics live in `namespace RSDK`, so without this the gate reads
    # None and false-REDs. A `::`-terminated prefix can't suffix-match a longer C
    # symbol (those have no `::`), so plain witnesses still resolve unchanged.
    pat = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+(?:[\w]+::)?_?" + re.escape(bare) + r"(?:\s|=|$)",
                     re.M)
    m = pat.search(map_text)
    return (int(m.group(1), 16) & 0xFFFFFFFF) if m else None


def load_harness():
    spec = importlib.util.spec_from_file_location("mcs_extract", MCS_EXTRACT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def calibrate(raw_magic):
    if raw_magic is None or len(raw_magic) < 4:
        return None, None
    raw4 = bytes(raw_magic[:4])
    for label, perm in PERMS:
        if bytes(MAGIC_BE[i] for i in perm) == raw4:
            return label, perm
    return None, None


def decode_u32(raw4, perm):
    inv = [perm.index(j) for j in range(4)]
    true_be = bytes(raw4[inv[j]] for j in range(4))
    return struct.unpack(">I", true_be)[0]


def decode_i32(raw4, perm):
    v = decode_u32(raw4, perm)
    return v - 0x100000000 if v & 0x80000000 else v


def peek_u32(mod, sections, addr, perm, signed=False):
    raw = mod._peek_bytes(sections, addr, 4)
    if raw is None or len(raw) < 4:
        return None
    return decode_i32(bytes(raw[:4]), perm) if signed else decode_u32(bytes(raw[:4]), perm)


def _hx(v):
    return "None" if v is None else ("0x%08X" % (v & 0xFFFFFFFF))


def _dv(v):
    return "None" if v is None else str(v)


def evaluate(pc, text_lo, text_hi, vals):
    """vals: dict symbol->decoded int. Returns (ok, [(name, ok, detail)]).
    Only the REQUIRED witnesses gate; diagnostics are reported separately."""
    checks = []

    c0 = (pc is not None and text_lo is not None and text_hi is not None
          and text_lo <= pc < text_hi)
    checks.append(("W0 SH2-M PC in core .text [%s,%s) (no crash at capture)"
                   % (_hx(text_lo), _hx(text_hi)), c0, "PC=%s" % _hx(pc)))

    got_loaded = vals.get(SYM_LOADED)
    checks.append((
        "W1 engine LoadSceneAssets(Title/Scene1.bin) ran to completion on SH-2",
        got_loaded is not None and got_loaded == EXP_LOADED,
        "%s = %s (expect %s)" % (SYM_LOADED, _dv(got_loaded), _dv(EXP_LOADED))))

    wn = 2
    for s in COORD_SYMS:
        got = vals.get(s)
        exp = EXP_COORD[s]
        checks.append((
            "W%d %s parsed byte-exact into objectEntityList[]" % (wn, COORD_LABEL[s]),
            got is not None and (got & 0xFFFFFFFF) == exp,
            "%s = %s (expect %s)" % (s, _hx(got), _hx(exp))))
        wn += 1

    ok = all(c for _, c, _ in checks)
    return ok, checks


def _print_checks(checks):
    for name, ok, detail in checks:
        print("  [%s] %s" % ("GREEN" if ok else " RED ", name))
        print("          %s" % detail)


def _print_diagnostics(vals):
    print("  diagnostics (not gated -- self-diagnose a failed GREEN):")
    print("          %s = %s  (1 InitStorage / 2 globals / 3 LoadSceneAssets / 4 copied)"
          % (SYM_STEP, _dv(vals.get(SYM_STEP))))
    print("          %s = %s  (InitStorage return; 1 == all engine pools malloc'd)"
          % (SYM_INITSTOR, _dv(vals.get(SYM_INITSTOR))))


def run_selftest():
    print("=" * 72)
    print("P6.3 LOADSCENE GATE -- SELFTEST (prove the RED path fires)")
    print("=" * 72)
    print("  expect (GREEN): loaded=1  sonic=(%s,%s)  emblem=(%s,%s)  t3d=(%s,%s)"
          % (_hx(EXP_SONIC_X), _hx(EXP_SONIC_Y), _hx(EXP_EMBLEM_X),
             _hx(EXP_EMBLEM_Y), _hx(EXP_T3D_X), _hx(EXP_T3D_Y)))
    print("  RED build: no P6SCENE link -> LoadSceneAssets never runs -> all scene")
    print("             witnesses 0.")
    print("-" * 72)
    text_lo, text_hi = TEXT_LOAD, 0x06030000
    # RED capture: scene witnesses never written (chain not wired) -> all 0.
    red_vals = {SYM_MAGIC: MAGIC_VALUE, SYM_LOADED: 0, SYM_STEP: 0,
                SYM_INITSTOR: 0}
    for s in COORD_SYMS:
        red_vals[s] = 0
    red_ok, red_checks = evaluate(0x06004F00, text_lo, text_hi, red_vals)
    _print_checks(red_checks)
    _print_diagnostics(red_vals)
    # GREEN capture: engine scene parser placed every coordinate.
    green_vals = {SYM_MAGIC: MAGIC_VALUE, SYM_LOADED: EXP_LOADED, SYM_STEP: 4,
                  SYM_INITSTOR: 1}
    for s in COORD_SYMS:
        green_vals[s] = EXP_COORD[s]
    green_ok, _ = evaluate(0x06004F00, text_lo, text_hi, green_vals)
    print("-" * 72)
    if (not red_ok) and green_ok:
        print("RESULT: RED (selftest) -- the unwired (no-P6SCENE) capture is correctly")
        print("        REJECTED (W1 loaded==0 + all six coords 0: LoadSceneAssets never")
        print("        ran) while a synthetic GREEN capture (engine scene parser placed")
        print("        every coordinate) passes. The W0-W7 RED branch is reachable; the")
        print("        gate distinguishes a real on-SH-2 scene parse from the unwired tree.")
        return 1
    print("RESULT: ERROR -- selftest logic inconsistent (red_ok=%s green_ok=%s)"
          % (red_ok, green_ok))
    return 2


def main(argv):
    if "--selftest" in argv:
        return run_selftest()

    pos = [a for a in argv if not a.startswith("--")]
    mcs = pos[0] if len(pos) >= 1 else MCS_DEFAULT
    mp = pos[1] if len(pos) >= 2 else MAP_DEFAULT

    print("=" * 72)
    print("P6.3 LOADSCENE GATE: engine LoadSceneAssets parses a real Scene.bin (Mednafen)")
    print("=" * 72)
    print("  savestate : %s" % mcs)
    print("  link map  : %s" % mp)
    print("  model     : loaded=1  sonic=(%s,%s) emblem=(%s,%s) t3d=(%s,%s)"
          % (_hx(EXP_SONIC_X), _hx(EXP_SONIC_Y), _hx(EXP_EMBLEM_X),
             _hx(EXP_EMBLEM_Y), _hx(EXP_T3D_X), _hx(EXP_T3D_Y)))
    print("-" * 72)

    if not os.path.isfile(mp):
        print("RESULT: RED -- P6 scene link map missing (%s)." % mp)
        print("        Build it:  make P6SCENE=1   (Path A: hosts the engine")
        print("        LoadScene closure in the proven jo image).")
        return 1
    if not os.path.isfile(mcs):
        print("RESULT: RED -- P6 scene savestate missing (%s)." % mcs)
        print("        Build the P6SCENE ISO and capture a post-LoadSceneAssets state:")
        print("        make P6SCENE=1 && <build ISO>")
        print("        pwsh -File tools/qa_savestate.ps1 -Cue <p6scene>.cue \\")
        print("             -SaveFrame 50 -FpsScale 2.0 \\")
        print("             -Out tools/_portspike/_p6/_p6_scene.mcs")
        print("        (deep capture: jo boot + GFS_Init + scene parse take many")
        print("         emulated seconds; an early SaveFrame catches PC pre-load.)")
        return 1

    map_text = read_text(mp)

    text_lo = map_symbol(map_text, SYM_TEXT_LO)
    if text_lo is None:
        text_lo = map_symbol(map_text, SYM_TEXT_LO_FB)
    text_hi = map_symbol(map_text, SYM_TEXT_HI)
    if text_hi is None:
        text_hi = map_symbol(map_text, SYM_TEXT_HI_FB)

    syms = {SYM_TEXT_LO: text_lo, SYM_TEXT_HI: text_hi}
    missing = []
    if text_lo is None:
        missing.append("%s (or fallback %s)" % (SYM_TEXT_LO, SYM_TEXT_LO_FB))
    if text_hi is None:
        missing.append("%s (or fallback %s)" % (SYM_TEXT_HI, SYM_TEXT_HI_FB))
    syms[SYM_MAGIC] = map_symbol(map_text, SYM_MAGIC)
    if syms[SYM_MAGIC] is None:
        missing.append(SYM_MAGIC)
    for s in WITNESS_SYMS:
        a = map_symbol(map_text, s)
        syms[s] = a
        if a is None:
            missing.append(s)
    if missing:
        print("RESULT: RED -- witness symbol(s) absent from the P6 scene map:")
        for s in missing:
            print("          %s" % s)
        print("        The scene witnesses / text bounds are not wired into the link.")
        print("        (Expected on the shipping build -- P6SCENE not set. Build with")
        print("        P6SCENE=1, re-link, re-capture.)")
        return 1

    print("[1/3] Witness symbols resolved from the link map:")
    for s in (SYM_TEXT_LO, SYM_TEXT_HI, SYM_MAGIC, *WITNESS_SYMS):
        print("        %-24s %s" % (s, _hx(syms[s])))

    mod = load_harness()
    sections = mod.parse_savestate(_as_path(mcs))

    raw_magic = mod._peek_bytes(sections, syms[SYM_MAGIC], 4)
    label, perm = calibrate(raw_magic)
    print("[2/3] WRAM byte-order calibration from _p6_w_magic:")
    if perm is None:
        print("        magic raw bytes = %s (expected a permutation of %s)"
              % (raw_magic.hex() if raw_magic else "None", MAGIC_BE.hex()))
        print("RESULT: RED -- the magic anchor (0x%08X) did not read back as any"
              % MAGIC_VALUE)
        print("        known byte permutation. The image did not load / witnesses")
        print("        are not in the expected bank / harness not linked.")
        return 1
    print("        magic raw = %s -> transform: %s" % (raw_magic.hex(), label))

    regs = mod._sh2_regs(sections, "master")
    pc = regs.get("PC") if regs else None

    # loaded/step/initstorage/objcount are signed scalars; the six coords are
    # positive fixed-point i32 -- read unsigned and compare to the disk u32.
    signed = {SYM_LOADED, SYM_STEP, SYM_INITSTOR}
    vals = {}
    for s in WITNESS_SYMS:
        vals[s] = peek_u32(mod, sections, syms[s], perm, signed=(s in signed))

    print("        peeked witnesses:")
    for s in WITNESS_SYMS:
        show = _dv(vals[s]) if s in signed else _hx(vals[s])
        print("          %-24s = %s" % (s, show))

    ok, checks = evaluate(pc, syms[SYM_TEXT_LO], syms[SYM_TEXT_HI], vals)

    print("[3/3] Engine scene-parse witnesses:")
    print("-" * 72)
    _print_checks(checks)
    _print_diagnostics(vals)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the UNMODIFIED engine LoadSceneAssets opened the real")
        print("        Title/Scene1.bin over the Saturn GFS backend, walked its object")
        print("        table + per-var type stream on SH-2, and placed every entity")
        print("        position byte-exact into objectEntityList[] (TitleSonic,")
        print("        TitleLogo, Title3DSprite all match the offline parse). P6.3 done")
        print("        -- STOP and report for the P6 checkpoint (P6.4-P6.7 proceed only")
        print("        under user direction).")
        return 0
    print("RESULT: RED -- the captured state does not satisfy all scene-parse")
    print("        witnesses; LoadSceneAssets did not parse the real Scene.bin into")
    print("        objectEntityList[] as modelled (unwired tree, or a regression).")
    return 1


def _as_path(p):
    try:
        from pathlib import Path
        return Path(p)
    except Exception:
        return p


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
