#!/usr/bin/env python3
# =============================================================================
# qa_p6_sglarea.py -- P6.7d.2 gate (Task #210): ENGINE-SIZED SGL WORK AREA.
# The stock SGLAREA.O parameter block reserves 0x060C0000..0x06100000 (256 KB)
# sized for MaxPolygons=1761/MaxVertices=2500 3D scenes (decoded from the .O:
# SpriteBuf 127,152 B + CommandBuf 50,236 B + Pbuffer 40,000 B + SortList
# 21,204 B + TransList region). The engine image draws ~70 VDP1 commands per
# frame, so per the OFFICIAL sizing formulas (SGL302 DOC/210A_US/WORKAREA.TXT
# sections 1-7 + the canonical SAMPLE/WORKAREA/WORKAREA.C form) the replacement
# block platform/Saturn/SaturnSGLArea.c is parameterized MAX_POLYGONS=144 /
# MAX_VERTICES=384 at WORK_AREA=0x060F4000 -- freeing 0x060C0000..0x060F4000
# = 212,992 B of WRAM-H for the P6.8 code window + packed collision (the W4
# closer, SaturnMemoryMap.h sizing record).
#
#   G1 the LINKED parameter block is the engine-sized one: MaxPolygons==144,
#      SortListSize==(144+6)*12==1800, SpriteBufSize==36*(144+6)*2==10800,
#      SortList==0x060F4000 -- peeked from the savestate at the map symbols.
#      RED on the stock block (1761 / 21204 / 127152 / 0x060C0000).
#   G2 sortlist-overflow headroom (the Phase 2.3e suppression class, doc
#      section 6: MaxPolygons bounds the per-frame overflow check): the VDP1
#      command table in VRAM walks to its END bit in < MaxPolygons commands.
#   G3 the freed region is real: _end (game.map) may now exceed the OLD
#      0x060C0000 floor but MUST stay below WORK_AREA 0x060F4000.
#
# Usage: python tools/_portspike/qa_p6_sglarea.py [savestate.mcs] [map]
# =============================================================================
import importlib.util
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

MCS_DEFAULT = os.path.join(HERE, "_p6", "_p6_pack.mcs")

MAX_POLYGONS = 144
MAX_VERTICES = 384
WORK_AREA = 0x060F4000
EXP_SORTLISTSIZE = (MAX_POLYGONS + 6) * 12        # doc sec 1 / sample formula
EXP_SPRITEBUFSIZE = 36 * (MAX_POLYGONS + 6) * 2   # doc sec 4 / sample formula

SYMS_U32 = ["_SortList", "_SortListSize", "_SpriteBufSize"]
SYM_MAXPOLY = "_MaxPolygons"  # Uint16


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.7d.2 SGL-AREA GATE: engine-sized work area (WORKAREA.TXT formulas)")
    print("=" * 72)
    print("  savestate: %s" % mcs)
    print("-" * 72)

    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s)" % mp)
        return 1
    map_text = _scene.read_text(mp)
    syms = {}
    missing = []
    for s in [_scene.SYM_MAGIC, SYM_MAXPOLY] + SYMS_U32:
        syms[s] = _scene.map_symbol(map_text, s)
        if syms[s] is None:
            missing.append(s)
    if missing:
        print("RESULT: RED -- symbol(s) absent from the map: %s" % ", ".join(missing))
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

    v = {s: _scene.peek_u32(mod, sections, syms[s], perm) for s in SYMS_U32}
    # MaxPolygons is a Uint16: read the containing u32 and take the right half
    # per the calibrated byte order (the params are 2-aligned in .rodata).
    mp_raw = _scene.peek_u32(mod, sections, syms[SYM_MAXPOLY] & ~3, perm)
    shift = 16 if (syms[SYM_MAXPOLY] & 2) == 0 else 0
    maxpoly = (mp_raw >> shift) & 0xFFFF
    print("  peeked: MaxPolygons=%d SortList=0x%08X SortListSize=%d SpriteBufSize=%d"
          % (maxpoly, v["_SortList"], v["_SortListSize"], v["_SpriteBufSize"]))

    # G2: walk the VDP1 command table from the savestate VRAM dump.
    import subprocess
    import tempfile
    cmdcount = -1
    with tempfile.TemporaryDirectory() as td:
        out = os.path.join(td, "v1.bin")
        r = subprocess.run([sys.executable,
                            os.path.join(HERE, "..", "mcs_extract.py"),
                            mcs, "--vdp1-vram", out],
                           capture_output=True, text=True)
        if os.path.isfile(out):
            # MEASURED (2026-06-11): the Mednafen VDP1 VRAM serialization is
            # BYTE-swapped per 16-bit word (raw cmd0 0x0910 -> 0x1009 = JS=1
            # system clip; 0x0810 -> 0x1008 user clip; 0x0A10 -> 0x100A local
            # coords; 0x0080 -> 0x8000 = the END command, ST-013 CMDCTRL
            # bit 15). Walk the table counting commands to END.
            vr = open(out, "rb").read()
            n = 0
            while n < 2048:
                raw = struct.unpack_from(">H", vr, n * 0x20)[0]
                ctrl = ((raw & 0xFF) << 8) | (raw >> 8)
                if ctrl & 0x8000:
                    break
                n += 1
            if 0 < n < 2048:
                cmdcount = n
    print("  VDP1 command-table walk: %s commands to END"
          % (cmdcount if cmdcount >= 0 else "unreadable"))

    end_m = None
    import re
    m = re.search(r"0x([0-9a-fA-F]+)\s+_end = \.", map_text)
    if m:
        end_m = int(m.group(1), 16)
    print("  _end = %s" % (hex(end_m) if end_m else "n/a"))

    checks = [
        ("G1 engine-sized block linked: MaxPolygons==%d, SortListSize==%d, "
         "SpriteBufSize==%d, SortList==0x%08X"
         % (MAX_POLYGONS, EXP_SORTLISTSIZE, EXP_SPRITEBUFSIZE, WORK_AREA),
         maxpoly == MAX_POLYGONS
         and v["_SortListSize"] == EXP_SORTLISTSIZE
         and v["_SpriteBufSize"] == EXP_SPRITEBUFSIZE
         and v["_SortList"] == WORK_AREA,
         "got poly=%d sls=%d sbs=%d sl=0x%08X"
         % (maxpoly, v["_SortListSize"], v["_SpriteBufSize"], v["_SortList"])),
        ("G2 per-frame VDP1 commands < MaxPolygons (sortlist-overflow headroom)",
         0 < cmdcount < MAX_POLYGONS, "count=%s" % cmdcount),
        ("G3 _end below WORK_AREA 0x%08X (freed region honored)" % WORK_AREA,
         end_m is not None and end_m < WORK_AREA, "_end=%s" % hex(end_m or 0)),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the engine-sized SGL work area is linked, the")
        print("        per-frame command load fits the new ceiling, and the")
        print("        212,992 B freed region is real.")
        return 0
    print("RESULT: RED -- engine-sized SGL area not proven (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
