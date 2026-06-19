#!/usr/bin/env python3
"""
qa_p6_mapoverlap.py -- P6.7 W12b root-cause gate (Task #227, 2026-06-12).

THE BUG CLASS THIS CATCHES: the pack's -fdata-sections named sections
(.bss._ZN..., .data._ZN..., .text.*) leave the `ld -r --gc-sections` pack
link as separate input sections; the FINAL jo link (COFF-SH output,
sgl.linker script from the do-not-touch COMMON tree) places them as ORPHAN
OUTPUT SECTIONS whose address ranges OVERLAP the main .bss/.data output
sections (MEASURED: p6_typeGroupsBacking [0x06065170,0x06075B80) overlapped
jo's LTO .bss containing __jo_fs_work at 0x0607515C; the LoadSceneFolder
type-group clear's typeGroups[126].entryCount=0 store landed EXACTLY on the
GfsMng access-server GFCF_Seek pointer (gfs_mng+12) -> every GFS_Seek
dispatch jumped through NULL -> the whole "W12b layout-sensitive crash"
bisect chain).

GATE: parse game.map output sections (name, addr, size); assert NO TWO
ALLOCATED RAM sections overlap. Exit 0 GREEN / 1 RED with the offending
pairs. Runs on every diag build; verify_done wires it next to the other
P6 savestate gates.
"""
import re
import sys
import os

MAP_DEFAULT = os.path.join(os.path.dirname(__file__), "..", "..", "game.map")

# Output-section header lines: `^<name> <addr> <size>` possibly wrapped to a
# second line when the name is long. Allocated RAM = addr in WRAM-L/H or LWRAM.
SEC_RE_ONE = re.compile(r"^([.][\w.$:*-]+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)")
SEC_RE_NAME = re.compile(r"^([.][\w.$:*-]+)\s*$")
SEC_RE_CONT = re.compile(r"^\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)")


def is_ram(addr):
    return (0x06000000 <= addr < 0x06100000) or (0x00200000 <= addr < 0x00300000)


def parse_sections(path):
    secs = []
    pending = None
    for line in open(path, "r", errors="replace"):
        line = line.rstrip("\n")
        if pending is not None:
            m = SEC_RE_CONT.match(line)
            if m:
                secs.append((pending, int(m.group(1), 16), int(m.group(2), 16)))
            pending = None
            continue
        m = SEC_RE_ONE.match(line)
        if m:
            secs.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16)))
            continue
        m = SEC_RE_NAME.match(line)
        if m:
            pending = m.group(1)
    # allocated, nonzero-size RAM sections only; drop debug/comment sections
    out = []
    for name, addr, size in secs:
        if size == 0 or not is_ram(addr):
            continue
        if name.startswith(".debug") or name in (".comment",):
            continue
        out.append((name, addr, size))
    return out


def main(argv):
    mp = argv[1] if len(argv) > 1 else MAP_DEFAULT
    print("=" * 72)
    print("P6 MAP-OVERLAP GATE: no two allocated output sections may overlap")
    print("=" * 72)
    print("  map: %s" % mp)
    if not os.path.isfile(mp):
        print("RESULT: RED -- map missing")
        return 1
    secs = sorted(parse_sections(mp), key=lambda s: s[1])
    print("  allocated RAM output sections: %d" % len(secs))
    bad = []
    # running-max sweep: catches sections NESTED inside a larger earlier one
    # (the real W12b shape: every pack orphan sat inside the main .bss span)
    cur_name, cur_addr, cur_end = None, 0, 0
    for n2, a2, s2 in secs:
        if cur_name is not None and a2 < cur_end:
            bad.append((cur_name, cur_addr, cur_end - cur_addr, n2, a2, s2))
        if a2 + s2 > cur_end:
            cur_name, cur_addr, cur_end = n2, a2, a2 + s2
    for n1, a1, s1, n2, a2, s2 in bad[:20]:
        print("  OVERLAP: %s [0x%08X,0x%08X) <-> %s [0x%08X,0x%08X)"
              % (n1, a1, a1 + s1, n2, a2, a2 + s2))
    if bad:
        print("RESULT: RED -- %d overlapping output-section pairs" % len(bad))
        print("  (orphan .bss.*/.data.* from the pack overlapping the main")
        print("   .bss/.data -- the W12b GFS-table-clobber class. Fix = the")
        print("   p6_pack_merge.ld second `ld -r` pass in build_p6scene_objs.sh.)")
        return 1
    # Task #227 STG sizing: the linked image must end BELOW the fixed WRAM-H ANIMPAK
    # load address or the anim-pack boot load clobbers live .bss (MEASURED p6_g2:
    # scene_step froze at 1).
    #
    # 2026-06-19 CORRECTION (RED-first for the #228 boot-trap class): the REAL clobber
    # line is P6_HW_ANIMPAK ITSELF (Animation.hpp), NOT the stale hardcoded 0x060B8000
    # this gate used. MEASURED: a diagnostic that pushed _end 0x060B65C0 -> 0x060B6800
    # (just 0x200 OVER P6_HW_ANIMPAK=0x060B6600) made the anim-pack load corrupt .bss ->
    # master SH-2 trapped at 0x06000956 (#228), blue screen forever, cont_frames=0. The
    # old 0x060B8000 ceiling sat 0x1A00 ABOVE the real line, so it would have passed that
    # trapping build (only the 16 KB headroom coincidentally RED'd it -- and that same
    # headroom FALSE-RED's working builds that legitimately run near the ceiling). Fix:
    # parse the LIVE ANIMPAK base and assert _end < it as the HARD gate -- this static map
    # check catches the trap WITHOUT a capture. The growth headroom is demoted to an
    # ADVISORY note (the tree runs near the ceiling until the pool/re-budget frees WRAM-H;
    # it must not fail a booting build).
    animpak = None
    anim_hpp = os.path.join(os.path.dirname(__file__), "..", "..",
                            "rsdkv5-src", "RSDKv5", "RSDK", "Graphics", "Animation.hpp")
    try:
        with open(anim_hpp, errors="ignore") as f:
            ma = re.search(r"#define\s+P6_HW_ANIMPAK\s+(0x[0-9a-fA-F]+)", f.read())
        if ma:
            animpak = int(ma.group(1), 16) & 0xFFFFFFFF
    except OSError:
        pass
    ADVISORY_MARGIN = 0x1000  # below this, print a re-budget advisory (NOT a RED)
    with open(mp, errors="ignore") as f:
        text = f.read()
    if "p6_scene_run" in text and animpak is not None:
        m = re.search(r"0x[0]*([0-9a-fA-F]+)\s+_end = \.", text)
        if m:
            end = int(m.group(1), 16)
            if end >= animpak:
                print("RESULT: RED -- BOOT TRAP: _end 0x%08X >= P6_HW_ANIMPAK 0x%08X -- the "
                      "anim-pack load clobbers live .bss (#228; master SH-2 traps 0x06000956, "
                      "blue screen, cont_frames=0). Reclaim WRAM-H before adding to it."
                      % (end, animpak))
                return 1
            margin = animpak - end
            adv = ("  [ADVISORY] only %d B WRAM-H headroom under ANIMPAK -- re-budget/pool "
                   "shrink needed before further growth" % margin) if margin < ADVISORY_MARGIN else ""
            print("  _end 0x%08X < P6_HW_ANIMPAK 0x%08X (headroom %d B)%s"
                  % (end, animpak, margin, ("\n" + adv) if adv else ""))
    elif "p6_scene_run" in text and animpak is None:
        print("  [warn] could not parse P6_HW_ANIMPAK from Animation.hpp -- _end clobber "
              "check SKIPPED (update the regex).")
    print("RESULT: GREEN -- all allocated output sections disjoint")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
