#!/usr/bin/env python3
# =============================================================================
# qa_p6_pack.py -- P6.4 gate (Task #225): the UNMODIFIED engine mounts the
# ORIGINAL Data.rsdk pack on the Saturn and loads the Title scene THROUGH it.
#
# WHAT IT PROVES (GREEN):
#   1. RSDK::LoadDataPack("Data.rsdk") ran on SH-2 against the on-disc pack
#      (cd/DATA.RSDK staged verbatim from the Steam Data.rsdk, 182,962,115 B)
#      and registered its file table: p6_w_pack_mounted == 1.
#   2. The registry size matches the REAL pack: p6_w_pack_filecount == 1677
#      (MEASURED 2026-06-10 from the Data.rsdk header: magic 'RSDKv5',
#      uint16 fileCount at offset 6 == 1677). This is what forced the
#      DATAFILE_COUNT Saturn cap 0x100 -> 0x700 (Reader.hpp:77).
#   3. The scene LoadFile routed THROUGH the pack, not the loose-file
#      fallback: p6_w_pack_used == 1 (witnessed from the post-LoadFile
#      FileInfo by p6_scene_run; a loose cd/SCENE1.BIN open cannot set it).
#   4. The pack-routed parse is still byte-exact: the SAME six entity-coord
#      witnesses as the P6.3 gate (TitleLogo slot 5, TitleSonic slot 8,
#      Title3DSprite slot 16) plus p6_w_scene_loaded -- hash lookup, per-file
#      offset/seek windowing, and (if flagged) per-file decryption all sit on
#      the read path, so one flipped byte fails a coordinate.
#
# RED fires when: any witness symbol is absent from game.map (the CURRENT
# state -- the P6.4 body is unwritten), the capture is missing, the magic
# mis-decodes, PC is outside [___Start, __bstart), or any value mismatches.
#
# Machinery (map parse, byte-order calibration, savestate peeks, W0 text
# bounds) is IMPORTED from the proven P6.3 gate qa_p6_scene.py -- one source
# of truth for the harness; this file owns only the P6.4 witness contract.
#
# Usage:
#   python tools/_portspike/qa_p6_pack.py [savestate.mcs] [link.map]
# Capture (after `make P6SCENE=1` + ISO build):
#   pwsh -File tools/qa_savestate.ps1 -Cue game.cue -SaveFrame 50 -FpsScale 2.0 \
#        -Out tools/_portspike/_p6/_p6_pack.mcs
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

# ---- P6.4 witness contract ---------------------------------------------------
EXP_FILECOUNT = 1677  # measured from Data.rsdk header (offset 6, LE uint16)

SYM_PACK_MOUNTED   = "_p6_w_pack_mounted"
SYM_PACK_FILECOUNT = "_p6_w_pack_filecount"
SYM_PACK_USED      = "_p6_w_pack_used"

PACK_SYMS = [SYM_PACK_MOUNTED, SYM_PACK_FILECOUNT, SYM_PACK_USED]
# Scene witnesses reused verbatim from the P6.3 contract (same symbols, same
# expected byte-exact values -- the load must now ride the pack).
SCENE_SYMS = [_scene.SYM_LOADED] + _scene.COORD_SYMS
ALL_SYMS = PACK_SYMS + SCENE_SYMS + _scene.DIAGNOSTIC_SYMS


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.4 PACK GATE: engine LoadDataPack mounts the ORIGINAL Data.rsdk")
    print("=" * 72)
    print("  savestate : %s" % mcs)
    print("  link map  : %s" % mp)
    print("  model     : mounted=1 filecount=%d used=1 + P6.3 scene witnesses"
          % EXP_FILECOUNT)
    print("-" * 72)

    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s). Build `make P6SCENE=1` first." % mp)
        return 1
    map_text = _scene.read_text(mp)

    # W0 text bounds: same primary/fallback ladder as the P6.3 gate.
    text_lo = _scene.map_symbol(map_text, _scene.SYM_TEXT_LO) \
        or _scene.map_symbol(map_text, _scene.SYM_TEXT_LO_FB)
    text_hi = _scene.map_symbol(map_text, _scene.SYM_TEXT_HI) \
        or _scene.map_symbol(map_text, _scene.SYM_TEXT_HI_FB)

    syms = {}
    missing = []
    if text_lo is None or text_hi is None:
        missing.append("text bounds (%s/%s or fallbacks)"
                       % (_scene.SYM_TEXT_LO, _scene.SYM_TEXT_HI))
    for s in [_scene.SYM_MAGIC] + ALL_SYMS:
        a = _scene.map_symbol(map_text, s)
        syms[s] = a
        if a is None:
            missing.append(s)

    print("[1/3] Witness symbols resolved from the link map:")
    for s in [_scene.SYM_MAGIC] + ALL_SYMS:
        print("        %-26s %s" % (s, ("0x%08X" % syms[s]) if syms[s] is not None else "ABSENT"))
    if missing:
        print("-" * 72)
        print("RESULT: RED -- witness symbol(s) absent from the P6 pack map:")
        for s in missing:
            print("        %s" % s)
        print("        (Expected while Task #225's p6_scene_run pack body is unwritten.)")
        return 1

    if not os.path.isfile(mcs):
        print("-" * 72)
        print("RESULT: RED -- P6 pack savestate missing (%s)." % mcs)
        print("        Build `make P6SCENE=1`, rebuild the ISO (cd/DATA.RSDK staged),")
        print("        then: pwsh -File tools/qa_savestate.ps1 -Cue game.cue \\")
        print("                  -SaveFrame 50 -FpsScale 2.0 -Out %s" % MCS_DEFAULT)
        return 1

    import pathlib
    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))

    raw_magic = mod._peek_bytes(sections, syms[_scene.SYM_MAGIC], 4)
    label, perm = _scene.calibrate(raw_magic)
    if perm is None:
        print("RESULT: RED -- magic word mis-decodes (raw=%s); capture corrupt?"
              % (raw_magic.hex() if raw_magic else "None"))
        return 1
    print("[2/3] byte-order calibration: %s" % label)

    regs = mod._sh2_regs(sections, "master")
    pc = regs.get("PC") if regs else None

    vals = {}
    for s in ALL_SYMS:
        signed = s in (_scene.SYM_LOADED, _scene.SYM_STEP, _scene.SYM_INITSTOR,
                       SYM_PACK_MOUNTED, SYM_PACK_USED)
        vals[s] = _scene.peek_u32(mod, sections, syms[s], perm, signed=signed)

    print("[2/3] peeked witnesses:")
    for s in ALL_SYMS:
        print("          %-26s = %s" % (s, _scene._hx(vals[s])
                                        if "coord" not in s else vals[s]))

    print("[3/3] P6.4 pack witnesses:")
    checks = []
    checks.append(("W0 SH2-M PC in loaded image [0x%08X,0x%08X)" % (text_lo, text_hi),
                   text_lo <= pc < text_hi, "PC=0x%08X" % pc))
    checks.append(("W1 LoadDataPack(Data.rsdk) mounted",
                   vals[SYM_PACK_MOUNTED] == 1,
                   "%s = %d (expect 1)" % (SYM_PACK_MOUNTED, vals[SYM_PACK_MOUNTED])))
    checks.append(("W2 pack registry == real Data.rsdk fileCount",
                   vals[SYM_PACK_FILECOUNT] == EXP_FILECOUNT,
                   "%s = %d (expect %d)" % (SYM_PACK_FILECOUNT,
                                            vals[SYM_PACK_FILECOUNT], EXP_FILECOUNT)))
    checks.append(("W3 scene LoadFile routed THROUGH the pack",
                   vals[SYM_PACK_USED] == 1,
                   "%s = %d (expect 1)" % (SYM_PACK_USED, vals[SYM_PACK_USED])))
    checks.append(("W4 LoadSceneAssets completed (via pack)",
                   vals[_scene.SYM_LOADED] == 1,
                   "%s = %d (expect 1)" % (_scene.SYM_LOADED, vals[_scene.SYM_LOADED])))
    for i, s in enumerate(_scene.COORD_SYMS):
        exp = _scene.EXP_COORD[s]
        checks.append(("W%d %s byte-exact via pack" % (5 + i, s),
                       (vals[s] & 0xFFFFFFFF) == exp,
                       "%s = 0x%08X (expect 0x%08X)" % (s, vals[s] & 0xFFFFFFFF, exp)))

    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("  diagnostics: %s=%d %s=%d" % (_scene.SYM_STEP, vals[_scene.SYM_STEP],
                                          _scene.SYM_INITSTOR, vals[_scene.SYM_INITSTOR]))
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the UNMODIFIED engine mounted the ORIGINAL")
        print("        Data.rsdk on Saturn hardware (emulated), resolved")
        print("        Data/Stages/Title/Scene1.bin by hash INSIDE the pack,")
        print("        and parsed it byte-exact. Original-asset ingestion is")
        print("        proven end-to-end. Next: P6.5 render backend.")
        return 0
    print("RESULT: RED -- pack-routed scene load not proven (see witnesses).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
