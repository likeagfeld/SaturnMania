#!/usr/bin/env python3
# =============================================================================
# gen_globals_map.py -- P6.7 wave-1 (Task #210): the GlobalVariables offset-
# translation generator + gate-model emitter.
#
# WHY: sizeof(GlobalVariables) is 268,148 B pre-Plus (MEASURED, P6.7d census
# toolchain) -- no Saturn bank holds it. Object code accesses fields BY NAME,
# so shrinking the four 64 KB buffers (SATURN_GLOBALS_RETARGET arms in
# SonicMania_GameVariables.h) is a pure layout retarget -- EXCEPT for the
# one place raw int32 offsets into the struct exist: the P6 engine builds at
# RETRO_REVISION=2 (build_p6scene_objs.sh CORE_DEFS beats RetroEngine.hpp:227's
# #ifndef default), where LoadGameConfig's REV02 seed loop writes
#   globalVarsPtr[offset + v] = ReadInt32(...)   (RetroEngine.cpp:1190-1197)
# with PC-layout offsets. UNMAPPED, the largest seed offset (67,032 int32 =
# 268,128 B) writes far past the 56,180 B Saturn window into the code region.
# The RETRO_SATURN arm of that loop remaps each offset through the table this
# script emits. (The v5U REV0U branch would discard seeds for an InitCB --
# NOT our build; S6 still proves seeds == the InitCB payload, so the two
# mechanisms are interchangeable for 1.03.)
#
# INPUTS
#   tools/_decomp_raw/SonicMania_GameVariables.h  (the struct, both arms)
#   extracted/Data/Game/GameConfig.bin            (1.03 REV01 layout; var
#       seeds parsed per the MEASURED end-to-end model in qa_p6_stagecfg.py)
#
# OUTPUTS
#   platform/Saturn/SaturnGlobalsMap.inc -- {pcOffset, satOffset} table
#       (int32-index units, sorted by pcOffset), consumed by the
#       RETRO_SATURN seed-remap arm in RetroEngine.cpp LoadGameConfig.
#   tools/_portspike/_p6/p6_globals_model.json -- gate model for
#       qa_p6_globals.py: saturn sizeof, per-seed {name, pc_off, sat_off,
#       values} so the gate peeks the seeded values at their Saturn-layout
#       byte addresses inside the fixed globals window, plus the full
#       saturn field table.
#
# SELF-TESTS (all must pass or the script exits nonzero; gate-grade facts):
#   S1 PC sizeof == 268,148 (the census-measured value)
#   S2 both layouts: field lists identical except the four retargeted arrays
#   S3 every seed offset lands INSIDE a field under the PC layout
#   S4 every seed (offset+v for all v) targets a SCALAR (field count == 1
#      or relative index fits the SATURN-sized field) -- the retarget's
#      correctness precondition
#   S5 saturn sizeof fits the fixed window budget (P6_GLOBALS_WINDOW_MAX)
#   S6 nonzero seed payload == the GlobalVariables_InitCB pre-Plus
#      assignment set (Game.c:50-67) -- the REV02-seed and v5U-InitCB
#      mechanisms agree for 1.03
# =============================================================================
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HDR = ROOT / "tools/_decomp_raw/SonicMania_GameVariables.h"
GAMECONFIG = ROOT / "extracted/Data/Game/GameConfig.bin"
OUT_INC = ROOT / "platform/Saturn/SaturnGlobalsMap.inc"
OUT_MODEL = Path(__file__).resolve().parent / "p6_globals_model.json"

# S6 expectation: GlobalVariables_InitCB (SonicMania_Game.c:50-67) pre-Plus
# writes exactly these two nonzero fields after the memset.
INITCB_WRITES = {"saveSlotID": [255], "presenceID": [-1]}

# Pre-Plus 1.03 config (the P6 build: -DGAME_VERSION=3 -> MANIA_USE_PLUS=0,
# GAME_VERSION != VER_100 true).
PC_SIZEOF_EXPECT = 268148  # MEASURED: P6.7d.1 census toolchain sizeof probe
# The globals window: 0x060C8000 inside the P6.7d.2-freed region
# 0x060C0000..0x060F4000; overlay window ends at 0x060C8000, SGL area
# starts at 0x060F4000 -> budget = 0x060F4000 - 0x060C8000.
P6_GLOBALS_WINDOW_MAX = 0x060F4000 - 0x060C8000  # 180224 B

FIELD_RE = re.compile(
    r"^\s*int32\s+(\w+)\s*(?:\[(0x[0-9A-Fa-f]+|\d+)\])?\s*;")


def parse_struct(saturn_retarget):
    """cpp-sim over the GlobalVariables struct body; returns
    [(name, count, start_index)] in int32-index units."""
    text = HDR.read_text()
    body = text.split("typedef struct {", 1)[1].split("} GlobalVariables;")[0]
    fields = []
    idx = 0
    # stack of (this_branch_active, any_branch_taken)
    stack = []

    def active():
        return all(s[0] for s in stack)

    for line in body.splitlines():
        s = line.strip()
        if s.startswith("#ifdef SATURN_GLOBALS_RETARGET"):
            stack.append([saturn_retarget, saturn_retarget])
        elif s.startswith("#if MANIA_USE_PLUS"):
            stack.append([False, False])
        elif s.startswith("#if GAME_VERSION != VER_100"):
            stack.append([True, True])
        elif s.startswith("#if"):
            raise SystemExit("unhandled #if in struct body: %r" % s)
        elif s.startswith("#else"):
            stack[-1][0] = not stack[-1][1]
            stack[-1][1] = True
        elif s.startswith("#endif"):
            stack.pop()
        else:
            m = FIELD_RE.match(line)
            if m and active():
                name = m.group(1)
                count = int(m.group(2), 0) if m.group(2) else 1
                fields.append((name, count, idx))
                idx += count
    if stack:
        raise SystemExit("unbalanced preprocessor stack in struct body")
    return fields, idx  # idx = total int32 count


def parse_seeds(path):
    """REV01 GameConfig var-seed list: [(offset, [values...])]. The walk to
    the seed section mirrors qa_p6_stagecfg.parse_gameconfig (MEASURED
    end-to-end on the 1.03 file)."""
    def read_string(f):
        n = f.read(1)[0]
        return f.read(n).rstrip(b"\x00").decode("latin1")

    with open(path, "rb") as f:
        if f.read(4) != b"CFG\x00":
            raise SystemExit("GameConfig signature mismatch")
        read_string(f); read_string(f); read_string(f)
        f.read(1); f.read(2)
        objCnt = f.read(1)[0]
        for _ in range(objCnt):
            read_string(f)
        for _b in range(8):
            rows = struct.unpack("<H", f.read(2))[0]
            for r in range(0x10):
                if rows >> r & 1:
                    f.read(0x30)
        sfxCnt = f.read(1)[0]
        for _ in range(sfxCnt):
            read_string(f); f.read(1)
        f.read(2)
        catCount = f.read(1)[0]
        for _c in range(catCount):
            read_string(f)
            scnt = f.read(1)[0]
            for _s in range(scnt):
                read_string(f); read_string(f); read_string(f)
        varCount = f.read(1)[0]
        seeds = []
        for _ in range(varCount):
            off = struct.unpack("<i", f.read(4))[0]
            cnt = struct.unpack("<I", f.read(4))[0]
            vals = list(struct.unpack("<%di" % cnt, f.read(4 * cnt)))
            seeds.append((off, vals))
        leftover = len(f.read())
    if leftover:
        raise SystemExit("GameConfig desync: %d bytes left" % leftover)
    return seeds


def field_at(fields, off):
    for name, count, start in fields:
        if start <= off < start + count:
            return name, count, start
    return None


def main():
    pc_fields, pc_total = parse_struct(saturn_retarget=False)
    sat_fields, sat_total = parse_struct(saturn_retarget=True)

    # S1
    if pc_total * 4 != PC_SIZEOF_EXPECT:
        raise SystemExit("S1 FAIL: PC sizeof %d != %d" %
                         (pc_total * 4, PC_SIZEOF_EXPECT))
    # S2
    pc_names = [(n, c) for n, c, _ in pc_fields]
    sat_names = [(n, c) for n, c, _ in sat_fields]
    if [n for n, _ in pc_names] != [n for n, _ in sat_names]:
        raise SystemExit("S2 FAIL: field name lists differ")
    diff = [(a, b) for a, b in zip(pc_names, sat_names) if a[1] != b[1]]
    exp_diff = {"atlEntityData", "saveRAM", "menuParam", "competitionSession"}
    if {a[0] for a, _ in diff} != exp_diff:
        raise SystemExit("S2 FAIL: retargeted set %r != %r" %
                         ({a[0] for a, _ in diff}, exp_diff))
    sat_by_name = {n: (c, s) for n, c, s in sat_fields}

    seeds = parse_seeds(GAMECONFIG)
    rows = []
    model_seeds = []
    for off, vals in seeds:
        hit = field_at(pc_fields, off)
        if hit is None:  # S3
            raise SystemExit("S3 FAIL: seed offset %d outside struct" % off)
        name, pc_count, pc_start = hit
        rel = off - pc_start
        sat_count, sat_start = sat_by_name[name]
        if rel + len(vals) > sat_count:  # S4
            raise SystemExit(
                "S4 FAIL: seed %s rel %d + %d values exceeds saturn field "
                "count %d" % (name, rel, len(vals), sat_count))
        new_off = sat_start + rel
        rows.append((off, new_off, name, len(vals)))
        model_seeds.append({
            "field": name, "pc_off": off, "sat_off": new_off,
            "values": vals,
        })
    # S5
    if sat_total * 4 > P6_GLOBALS_WINDOW_MAX:
        raise SystemExit("S5 FAIL: saturn sizeof %d > window %d" %
                         (sat_total * 4, P6_GLOBALS_WINDOW_MAX))
    # S6: the v5U seed-discard loses nothing -- nonzero seed payload must
    # equal the InitCB pre-Plus assignment set exactly.
    nonzero_map = {}
    for s in model_seeds:
        if any(s["values"]):
            nonzero_map[s["field"]] = s["values"]
    if nonzero_map != INITCB_WRITES:
        raise SystemExit("S6 FAIL: nonzero seeds %r != InitCB writes %r" %
                         (nonzero_map, INITCB_WRITES))

    rows.sort()
    lines = []
    lines.append("// SaturnGlobalsMap.inc -- GENERATED by "
                 "tools/_portspike/_p6/gen_globals_map.py. DO NOT EDIT.")
    lines.append("// P6.7 wave-1 (Task #210): GameConfig var-seed offset "
                 "translation for the")
    lines.append("// SATURN_GLOBALS_RETARGET GlobalVariables layout, "
                 "consumed by the RETRO_SATURN")
    lines.append("// arm of the REV02 seed loop (RetroEngine.cpp:1190-1197 "
                 "globalVarsPtr[offset+v]).")
    lines.append("// Units: int32 indices into the globals block. "
                 "%d seeds, every one scalar-" % len(rows))
    lines.append("// relative within its field (generator self-tests "
                 "S1-S6).")
    lines.append("#define SATURN_GLOBALS_PC_SIZE    %d" % (pc_total * 4))
    lines.append("#define SATURN_GLOBALS_SAT_SIZE   %d" % (sat_total * 4))
    lines.append("#define SATURN_GLOBALS_SEED_COUNT %d" % len(rows))
    lines.append("static const struct { int32 pcOffset; int32 satOffset; } "
                 "saturnGlobalsSeedMap[SATURN_GLOBALS_SEED_COUNT] = {")
    for off, new_off, name, cnt in rows:
        lines.append("    { %6d, %6d }, // %s (%d value%s)" %
                     (off, new_off, name, cnt, "" if cnt == 1 else "s"))
    lines.append("};")
    OUT_INC.write_text("\n".join(lines) + "\n")

    model = {
        "_comment": "GENERATED by gen_globals_map.py -- qa_p6_globals model",
        "pc_sizeof": pc_total * 4,
        "sat_sizeof": sat_total * 4,
        "seed_count": len(rows),
        "seeds": model_seeds,
        "sat_fields": [
            {"name": n, "count": c, "start": s} for n, c, s in sat_fields],
    }
    OUT_MODEL.write_text(json.dumps(model, indent=1))

    nonzero = sum(1 for s in model_seeds if any(s["values"]))
    print("OK: %d seeds mapped (%d with nonzero values); PC sizeof %d, "
          "Saturn sizeof %d (window budget %d, margin %d)" %
          (len(rows), nonzero, pc_total * 4, sat_total * 4,
           P6_GLOBALS_WINDOW_MAX, P6_GLOBALS_WINDOW_MAX - sat_total * 4))
    print("wrote %s + %s" % (OUT_INC, OUT_MODEL))
    return 0


if __name__ == "__main__":
    sys.exit(main())
