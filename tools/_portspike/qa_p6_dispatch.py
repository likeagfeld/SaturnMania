#!/usr/bin/env python3
# P6.1 gate (Task #205) -- prove the real decomp Ring's RSDK.* calls now travel
# THROUGH the engine's own dispatch table (RSDK::RSDKFunctionTable[]) populated by
# the REAL RSDK::SetupFunctionTables() (Core/Link.cpp:65), not via a hand bridge
# that calls the engine functions directly (as P5 did).
#
# WHY THIS IS THE P6.1 MILESTONE (engine-driven dispatch)
#   P5 (#204) proved the true-ported core can HOST a real decomp object: the
#   verbatim Ring_State_LostFX / Ring_Draw_Normal bodies ran under the engine's
#   ProcessObjects / ProcessObjectDrawLists, with their RSDK.ProcessAnimation /
#   RSDK.DrawSprite calls bridged DIRECTLY to the engine functions.
#   P6.1 (this) inserts the engine's OWN function table into that path: main()
#   calls the REAL SetupFunctionTables(), which writes every backend entry-point
#   address into RSDKFunctionTable[]; the two bridges then dispatch through
#   RSDKFunctionTable[FunctionTable_ProcessAnimation] / [FunctionTable_DrawSprite].
#   GREEN proves (a) the real table-population path links + runs on SH-2, and
#   (b) the object still runs end-to-end while its dispatch is table-driven.
#
# THE ELEVEN RUNTIME WITNESSES (all C-linkage globals, read from a Mednafen state)
#   The NINE carried verbatim from P5 (same expected values -> byte-identical
#   object behaviour, proving the table indirection changed dispatch ROUTE, not
#   RESULT):
#     p6_w_magic        const 0x12345678  -- WRAM byte-order self-calibration.
#     p6_w_ticks        driver ran ProcessObjects+ProcessObjectDrawLists N times.
#     p6_w_draw_calls   Ring_Draw_Normal's bridge fired each tick (== N). Bumped
#                       BEFORE the table dispatch, so it is N in BOTH builds --
#                       it proves the DRAW LIST reached the bridge, NOT that the
#                       table slot was populated (that is p6_w_last_frameid +
#                       the two slot witnesses).
#     p6_w_last_frameid animator.frameID at the final DrawSprite. ONLY advances
#                       if ProcessAnimation actually ran -> in the RED build the
#                       zero table makes the proc-anim bridge's `if(fn)` skip, so
#                       frameID never advances and this stays 0; GREEN == model.
#     p6_w_ring_classid / _timer / _scalex / _vely / _posy  -- the Ring state
#                       integration (identical to P5).
#   The TWO new P6.1 slot-address witnesses (the milestone proof):
#     p6_w_slot_procanim   RSDKFunctionTable[FunctionTable_ProcessAnimation], the
#                          address SetupFunctionTables wrote. GREEN: lands inside
#                          core .text [__text_start,__text_end) (the REAL
#                          ProcessAnimation, Animation.cpp). RED: 0 (empty table).
#     p6_w_slot_drawsprite RSDKFunctionTable[FunctionTable_DrawSprite]. GREEN: in
#                          .text (the no-op DrawSprite stub in p6_stubs.cpp -- the
#                          VDP1 rasterizer lands at P6.4). RED: 0.
#
# RED vs GREEN (one gate, two builds -- the RED->GREEN demonstration)
#   RED   (link_p6.sh red):   FunctionTables_Saturn.o (empty SetupFunctionTables,
#         zeroed table); NO Core_Link.o / p6_stubs.o. Table all-zero ->
#         slot witnesses 0 (W9/W10 RED) -> bridges skip -> ProcessAnimation never
#         runs -> last_frameid 0 (W8 RED). The gate REJECTS it.
#   GREEN (link_p6.sh green):  Core_Link.o (real SetupFunctionTables + table) +
#         p6_stubs.o (the 51 entry points); FunctionTables_Saturn.o excluded;
#         UserCore recompiled -DRSDK_SKU_GLOBALS_IN_LINK. Real table -> both slots
#         in .text -> ProcessAnimation dispatches -> last_frameid 6, all 9 Ring
#         witnesses match P5. The gate ACCEPTS it.
#
# RED-first (CLAUDE.md 4.7 / skill Step 7): on the current tree there is NO P6
# link map and NO P6 savestate, so the gate fires RED ("artifacts missing"). It
# also fires RED if a witness symbol is absent (harness/linker not wired), if the
# magic anchor does not read back (image did not load / wrong bank), if the SH-2
# PC is outside core .text (crash), or if ANY of the eleven witnesses disagrees
# with the model. It turns GREEN only for a real captured GREEN run.
#
#   python tools/_portspike/qa_p6_dispatch.py [savestate.mcs] [link.map]
#   python tools/_portspike/qa_p6_dispatch.py --selftest    # prove RED path fires
#
# P6 STATUS: this is P6.1 scaffolding (Saturn-only, build-gated; PC build
# untouched). GREEN here is the "engine dispatch works" checkpoint -- STOP and
# report; the P6.2-P6.7 climb (file I/O, real LoadScene, VDP1/VDP2, SCSP, decomp
# object set, hand-port retirement) proceeds only under user direction.

import importlib.util
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAP_DEFAULT = os.path.join(HERE, "_p6", "_p6.map")
MCS_DEFAULT = os.path.join(HERE, "_p6", "_p6_dispatch.mcs")
MCS_EXTRACT = os.path.normpath(os.path.join(HERE, "..", "mcs_extract.py"))

TEXT_LOAD = 0x06004000
WRAM_H = (0x06000000, 0x06100000)
WRAM_L = (0x00200000, 0x00300000)

# ---- Shared harness constants (MUST equal the values p6_main.cpp sets up) ------
N_TICKS            = 40       # driver loop count (< 65 so Ring_State_LostFX never destroys)
ANIM_SPEED         = 0x60     # animator.speed (added to timer each ProcessAnimation)
ANIM_FRAME_DURATION = 0x100   # uniform SpriteFrame.duration for all frames
ANIM_FRAME_COUNT   = 8        # animator.frameCount
ANIM_LOOP_INDEX    = 0        # animator.loopIndex

# ---- Deterministic expected witness values (identical to P5) -------------------
EXP_TICKS      = N_TICKS
EXP_DRAW_CALLS = N_TICKS
EXP_CLASSID    = 1                       # Ring registered at objectClassList index 1
EXP_TIMER      = N_TICKS                  # ++timer per tick, N < 65
EXP_SCALEX     = N_TICKS * 0x10           # scale.x += 0x10 per tick from 0
EXP_VELY       = N_TICKS * 0x1800         # velocity.y += 0x1800 per tick from 0
EXP_POSY       = 0x1800 * (N_TICKS * (N_TICKS + 1) // 2)

# Witness symbol names (sh-none-elf one-underscore convention; map_symbol tolerant).
SYM_TEXT_LO   = "__text_start"
SYM_TEXT_HI   = "__text_end"
SYM_MAGIC     = "_p6_w_magic"
SYM_TICKS     = "_p6_w_ticks"
SYM_DRAWS     = "_p6_w_draw_calls"
SYM_FRAMEID   = "_p6_w_last_frameid"
SYM_SLOT_PA   = "_p6_w_slot_procanim"
SYM_SLOT_DS   = "_p6_w_slot_drawsprite"
SYM_CLASSID   = "_p6_w_ring_classid"
SYM_TIMER     = "_p6_w_ring_timer"
SYM_SCALEX    = "_p6_w_ring_scalex"
SYM_VELY      = "_p6_w_ring_vely"
SYM_POSY      = "_p6_w_ring_posy"

WITNESS_SYMS = [SYM_MAGIC, SYM_TICKS, SYM_DRAWS, SYM_FRAMEID, SYM_SLOT_PA,
                SYM_SLOT_DS, SYM_CLASSID, SYM_TIMER, SYM_SCALEX, SYM_VELY, SYM_POSY]

MAGIC_VALUE = 0x12345678
MAGIC_BE = bytes([0x12, 0x34, 0x56, 0x78])
PERMS = [
    ("big-endian (identity)", (0, 1, 2, 3)),
    ("16-bit pair-swap", (1, 0, 3, 2)),
    ("full little-endian", (3, 2, 1, 0)),
    ("32-bit word-swap", (2, 3, 0, 1)),
]


def simulate_process_animation(n, speed, duration, frame_count, loop_index):
    """Bit-exact mirror of RSDK::ProcessAnimation (Animation.cpp) sprite path for
    `n` ticks with a uniform-duration synthetic SpriteFrame table. Returns the
    final frameID (the cadence witness the GREEN SH-2 build must match)."""
    frame_id = 0
    timer = 0
    frame_duration = duration  # frames[0].duration
    for _ in range(n):
        timer += speed
        while timer > frame_duration:
            frame_id += 1
            timer -= frame_duration
            if frame_id >= frame_count:
                frame_id = loop_index
            frame_duration = duration  # uniform table -> frames[frame_id].duration
    return frame_id


EXP_FRAMEID = simulate_process_animation(
    N_TICKS, ANIM_SPEED, ANIM_FRAME_DURATION, ANIM_FRAME_COUNT, ANIM_LOOP_INDEX)


def read_text(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def map_symbol(map_text, name):
    """Address of a defined symbol from GNU ld -Map output, tolerating the
    sh-none-elf one-underscore strip on object symbols."""
    bare = name[1:] if name.startswith("_") else name
    pat = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+_?" + re.escape(bare) + r"(?:\s|=|$)",
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
    """vals: dict symbol->decoded int. Returns (ok, [(name, ok, detail)])."""
    checks = []

    c0 = (pc is not None and text_lo is not None and text_hi is not None
          and text_lo <= pc < text_hi)
    checks.append(("W0 SH2-M PC in core .text [%s,%s) (no crash at capture)"
                   % (_hx(text_lo), _hx(text_hi)), c0, "PC=%s" % _hx(pc)))

    def cmp(sym, expect, label):
        got = vals.get(sym)
        ok = got is not None and got == expect
        checks.append((label, ok, "%s = %s (expect %s)"
                       % (sym, _dv(got), _dv(expect))))

    cmp(SYM_TICKS, EXP_TICKS,
        "W1 driver ran ProcessObjects+DrawLists N times")
    cmp(SYM_CLASSID, EXP_CLASSID,
        "W2 engine UPDATE dispatched a registered Ring (classID==1)")
    cmp(SYM_TIMER, EXP_TIMER,
        "W3 Ring_State_LostFX ran each tick (++timer==N)")
    cmp(SYM_SCALEX, EXP_SCALEX,
        "W4 Ring_State_LostFX scale.x math (==N*0x10)")
    cmp(SYM_VELY, EXP_VELY,
        "W5 Ring_State_LostFX velocity.y math (==N*0x1800)")
    cmp(SYM_POSY, EXP_POSY,
        "W6 Ring_State_LostFX integrated position.y (==0x1800*N(N+1)/2)")
    cmp(SYM_DRAWS, EXP_DRAW_CALLS,
        "W7 engine DRAW list reached Ring_Draw_Normal bridge (==N)")
    cmp(SYM_FRAMEID, EXP_FRAMEID,
        "W8 dispatch THROUGH RSDKFunctionTable[ProcessAnimation] advanced the "
        "animator (frameID==%d)" % EXP_FRAMEID)

    # --- P6.1 milestone: the two table slots hold live core .text addresses ----
    def slot_in_text(sym, label):
        got = vals.get(sym)
        ok = (got is not None and text_lo is not None and text_hi is not None
              and text_lo <= (got & 0xFFFFFFFF) < text_hi)
        checks.append((label, ok, "%s = %s (expect within [%s,%s))"
                       % (sym, _hx(got), _hx(text_lo), _hx(text_hi))))

    slot_in_text(SYM_SLOT_PA,
        "W9 SetupFunctionTables wrote RSDKFunctionTable[ProcessAnimation] -> .text")
    slot_in_text(SYM_SLOT_DS,
        "W10 SetupFunctionTables wrote RSDKFunctionTable[DrawSprite] -> .text")

    ok = all(c for _, c, _ in checks)
    return ok, checks


def _print_checks(checks):
    for name, ok, detail in checks:
        print("  [%s] %s" % ("GREEN" if ok else " RED ", name))
        print("          %s" % detail)


def run_selftest():
    print("=" * 72)
    print("P6.1 DISPATCH GATE -- SELFTEST (prove the RED path fires)")
    print("=" * 72)
    print("  model: N=%d speed=0x%X dur=0x%X frames=%d loop=%d -> frameID=%d"
          % (N_TICKS, ANIM_SPEED, ANIM_FRAME_DURATION, ANIM_FRAME_COUNT,
             ANIM_LOOP_INDEX, EXP_FRAMEID))
    print("  expect: ticks=%d draws=%d classid=%d timer=%d scalex=%d vely=%d posy=%d"
          % (EXP_TICKS, EXP_DRAW_CALLS, EXP_CLASSID, EXP_TIMER, EXP_SCALEX,
             EXP_VELY, EXP_POSY))
    print("  + both slot witnesses must land in [__text_start,__text_end)")
    print("-" * 72)
    text_lo, text_hi = TEXT_LOAD, 0x06030000
    # The RED capture: empty table. The Ring still UPDATES (engine drove it) and
    # the draw bridge still fires (draw_calls==N, frameID captured BEFORE the
    # skipped dispatch), but ProcessAnimation never ran (frameID 0) and both
    # table slots are 0 (not in .text). The gate MUST reject this.
    red_vals = {
        SYM_TICKS: EXP_TICKS, SYM_DRAWS: EXP_DRAW_CALLS, SYM_CLASSID: EXP_CLASSID,
        SYM_TIMER: EXP_TIMER, SYM_SCALEX: EXP_SCALEX, SYM_VELY: EXP_VELY,
        SYM_POSY: EXP_POSY, SYM_FRAMEID: 0, SYM_MAGIC: MAGIC_VALUE,
        SYM_SLOT_PA: 0, SYM_SLOT_DS: 0,
    }
    red_ok, red_checks = evaluate(0x06004F00, text_lo, text_hi, red_vals)
    _print_checks(red_checks)
    # The GREEN capture: real table -> slots in .text, frameID==model.
    green_vals = {
        SYM_TICKS: EXP_TICKS, SYM_DRAWS: EXP_DRAW_CALLS, SYM_CLASSID: EXP_CLASSID,
        SYM_TIMER: EXP_TIMER, SYM_SCALEX: EXP_SCALEX, SYM_VELY: EXP_VELY,
        SYM_POSY: EXP_POSY, SYM_FRAMEID: EXP_FRAMEID, SYM_MAGIC: MAGIC_VALUE,
        SYM_SLOT_PA: 0x06005120, SYM_SLOT_DS: 0x0600A4C0,
    }
    green_ok, _ = evaluate(0x06004F00, text_lo, text_hi, green_vals)
    print("-" * 72)
    if (not red_ok) and green_ok:
        print("RESULT: RED (selftest) -- the empty-table (RED build) capture is")
        print("        correctly REJECTED (W8 frameID==0, W9/W10 slots==0 -> not")
        print("        in .text) while a synthetic GREEN capture (real table)")
        print("        passes. The W0-W10 RED branch is reachable; the gate")
        print("        distinguishes table-driven dispatch from a zero table.")
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
    print("P6.1 DISPATCH GATE: Ring's RSDK.* routes through the engine table (Mednafen)")
    print("=" * 72)
    print("  savestate : %s" % mcs)
    print("  link map  : %s" % mp)
    print("  model     : N=%d -> frameID=%d, timer=%d, scalex=%d, vely=%d, posy=%d"
          % (N_TICKS, EXP_FRAMEID, EXP_TIMER, EXP_SCALEX, EXP_VELY, EXP_POSY))
    print("-" * 72)

    if not os.path.isfile(mp):
        print("RESULT: RED -- P6 link map missing (%s)." % mp)
        print("        Build it:  bash tools/_portspike/_p6/link_p6.sh green")
        return 1
    if not os.path.isfile(mcs):
        print("RESULT: RED -- P6 savestate missing (%s)." % mcs)
        print("        Build the P6 ISO and capture a post-loop state:")
        print("        bash tools/_portspike/_p6/build_p6_iso.sh green")
        print("        pwsh -File tools/qa_savestate.ps1 -Cue <p6>.cue \\")
        print("             -SaveFrame 12 -Out tools/_portspike/_p6/_p6_dispatch.mcs")
        return 1

    map_text = read_text(mp)

    syms = {}
    missing = []
    for s in (SYM_TEXT_LO, SYM_TEXT_HI, *WITNESS_SYMS):
        a = map_symbol(map_text, s)
        syms[s] = a
        if a is None:
            missing.append(s)
    if missing:
        print("RESULT: RED -- witness symbol(s) absent from the P6 map:")
        for s in missing:
            print("          %s" % s)
        print("        The harness witnesses / two-bank __text_start/__text_end are")
        print("        not wired into the P6 link. Add them, re-link, re-capture.")
        return 1

    print("[1/3] Witness symbols resolved from the link map:")
    for s in (SYM_TEXT_LO, SYM_TEXT_HI, *WITNESS_SYMS):
        print("        %-22s %s" % (s, _hx(syms[s])))

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

    # The two slot witnesses hold .text addresses (0x0600xxxx, positive int32) --
    # decode them UNSIGNED so the range check compares true addresses; the rest
    # are signed scalars.
    signed = {SYM_SCALEX, SYM_VELY, SYM_POSY, SYM_TIMER, SYM_FRAMEID,
              SYM_TICKS, SYM_DRAWS, SYM_CLASSID}
    vals = {}
    for s in WITNESS_SYMS:
        vals[s] = peek_u32(mod, sections, syms[s], perm, signed=(s in signed))

    print("        peeked witnesses:")
    for s in WITNESS_SYMS:
        show = _hx(vals[s]) if s in (SYM_SLOT_PA, SYM_SLOT_DS) else _dv(vals[s])
        print("          %-22s = %s" % (s, show))

    ok, checks = evaluate(pc, syms[SYM_TEXT_LO], syms[SYM_TEXT_HI], vals)

    print("[3/3] Engine-dispatch witnesses:")
    print("-" * 72)
    _print_checks(checks)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the real decomp Ring's RSDK.* calls now dispatch")
        print("        THROUGH the engine's own RSDKFunctionTable[], populated by")
        print("        the REAL SetupFunctionTables(): both table slots hold live")
        print("        core .text addresses, ProcessAnimation advanced the animator")
        print("        at decomp cadence (frameID==%d), and all 9 Ring witnesses" % EXP_FRAMEID)
        print("        match P5 byte-for-byte. P6.1 done -- STOP and report for the")
        print("        P6 checkpoint (P6.2-P6.7 proceed only under user direction).")
        return 0
    print("RESULT: RED -- the captured state does not satisfy all engine-dispatch")
    print("        witnesses; the Ring's RSDK.* did not route through a populated")
    print("        engine table as modelled (empty-table RED build, or a regression).")
    return 1


def _as_path(p):
    try:
        from pathlib import Path
        return Path(p)
    except Exception:
        return p


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
