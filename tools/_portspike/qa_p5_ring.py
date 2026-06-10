#!/usr/bin/env python3
# P5 gate (Task #204) -- prove a REAL decomp object (Objects/Global/Ring.c) runs
# end-to-end on the TRUE-PORTED RSDKv5 core: registered + spawned + DRIVEN by the
# real engine object system, with its verbatim decomp state/draw bodies executing
# and its animator advancing at the decomp ProcessAnimation cadence.
#
# WHY THIS IS THE P5 MILESTONE (the pivot's "first object renders" checkpoint)
#   P3 proved the core BOOTS (InitEngine + ProcessEngine through ENGINESTATE_LOAD).
#   P5 (this) proves the core can HOST GAME LOGIC: the unmodified decomp bodies
#   Ring_Update / Ring_Draw / Ring_State_LostFX / Ring_Draw_Normal (Ring.c lines
#   12-17, 23-28, 633-648, 777-783) compile against the true-ported core and are
#   invoked BY the real engine's RSDK::ProcessObjects + RSDK::ProcessObjectDrawLists
#   -- not by a hand driver calling them directly. The Ring's per-tick arithmetic
#   and its RSDK.ProcessAnimation cadence are reproduced bit-for-bit by a Python
#   model here; GREEN means the SH-2 build matched the model.
#
# THE VERBATIM SUBSET (dependency-free slice of the real Ring.c)
#   Ring_State_LostFX (Ring.c:633-648) per tick:
#       velocity.y += 0x1800;  position.x += velocity.x;  position.y += velocity.y;
#       RSDK.ProcessAnimation(&animator);  scale.x += 0x10;  scale.y += 0x10;
#       if (++timer > 64) destroyEntity();
#   Ring_Draw_Normal (Ring.c:777-783):
#       direction = animator.frameID > 8;  RSDK.DrawSprite(&animator, NULL, false);
#   These touch ONLY `self`, RSDK.ProcessAnimation (the REAL Animation.cpp func,
#   one of the 16 true-ported core TUs) and RSDK.DrawSprite (routed to a witness
#   shim -- the full VDP1 DrawSprite in Drawing.cpp is the P6 render surface). No
#   Zone/Player/Platform/collision coupling -> the full Ring_Create/StageLoad and
#   the rest of Ring.c's 929 lines are the P6 climb, NOT this bounded proof.
#
# THE NINE RUNTIME WITNESSES (all C-linkage globals, read from a Mednafen state)
#   p5_w_magic        const 0x12345678  -- WRAM byte-order self-calibration anchor.
#   p5_w_ticks        the driver ran ProcessObjects+ProcessObjectDrawLists N times.
#   p5_w_draw_calls   Ring_Draw->Ring_Draw_Normal->RSDK.DrawSprite fired each tick
#                     (== N) -> the engine's DRAW list reached the real draw body.
#   p5_w_last_frameid animator.frameID at the final DrawSprite == the Python
#                     ProcessAnimation model -> faithful sprite-anim CADENCE.
#   p5_w_ring_classid the spawned slot's classID (== 1) -> the engine UPDATE loop
#                     dispatched a registered Ring (not slot 0 / a stray).
#   p5_w_ring_timer   self->timer after N ticks (== N, N<65) -> Ring_State_LostFX
#                     ran and ++timer'd every tick (engine called update()).
#   p5_w_ring_scalex  self->scale.x (== N*0x10) -> the state body's scale math.
#   p5_w_ring_vely    self->velocity.y (== N*0x1800) -> the state body's vel math.
#   p5_w_ring_posy    self->position.y (== 0x1800 * N(N+1)/2) -> the INTEGRATED
#                     position; the hardest value to produce without actually
#                     executing the verbatim body N times in order.
#
# CADENCE MODEL (must mirror RSDK::ProcessAnimation, Animation.cpp, sprite path)
#   timer += speed; while (timer > frameDuration) { ++frameID; timer -= frameDuration;
#   if (frameID >= frameCount) frameID = loopIndex; frameDuration = frames[frameID].duration; }
#   The harness binds the spawned Ring's animator to a synthetic in-RAM
#   SpriteFrame[ANIM_FRAME_COUNT] with uniform .duration == ANIM_FRAME_DURATION,
#   speed == ANIM_SPEED, loopIndex == ANIM_LOOP_INDEX. Those four constants are
#   shared verbatim between this gate and p5_ring.cpp; the Python loop below and
#   the linked Animation.cpp must reach the SAME frameID after N ticks.
#
# RED-first (CLAUDE.md 4.7 / skill Step 7): on the current tree there is NO P5
# link map and NO P5 savestate, so the gate fires RED ("artifacts missing"). It
# also fires RED if any witness symbol is absent from the map (harness/linker not
# wired), if the magic anchor does not read back (image did not load / wrong
# bank), if the SH-2 PC is outside core .text (crash), or if ANY of the nine
# witnesses disagrees with the model (the object did not actually run, or ran
# wrong). It turns GREEN only when a real captured run satisfies all of them.
#
#   python tools/_portspike/qa_p5_ring.py [savestate.mcs] [link.map]
#   python tools/_portspike/qa_p5_ring.py --selftest   # prove the RED path fires
#
# P6 RESTORATION: p5_ring.cpp / p5_main.cpp / the witness globals / this gate are
# P5 scaffolding, Saturn-only and build-gated; the PC build is untouched. P6
# decides whether the true-port becomes the shipping path.

import importlib.util
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAP_DEFAULT = os.path.join(HERE, "_p5", "_p5.map")
MCS_DEFAULT = os.path.join(HERE, "_p5", "_p5_ring.mcs")
MCS_EXTRACT = os.path.normpath(os.path.join(HERE, "..", "mcs_extract.py"))

TEXT_LOAD = 0x06004000
WRAM_H = (0x06000000, 0x06100000)
WRAM_L = (0x00200000, 0x00300000)

# ---- Shared harness constants (MUST equal the #defines in p5_ring.cpp) --------
N_TICKS            = 40       # driver loop count (< 65 so Ring_State_LostFX never destroys)
ANIM_SPEED         = 0x60     # animator.speed (added to timer each ProcessAnimation)
ANIM_FRAME_DURATION = 0x100   # uniform SpriteFrame.duration for all frames
ANIM_FRAME_COUNT   = 8        # animator.frameCount
ANIM_LOOP_INDEX    = 0        # animator.loopIndex

# ---- Deterministic expected witness values -----------------------------------
EXP_TICKS      = N_TICKS
EXP_DRAW_CALLS = N_TICKS
EXP_CLASSID    = 1                       # Ring registered at objectClassList index 1
EXP_TIMER      = N_TICKS                  # ++timer per tick, N < 65
EXP_SCALEX     = N_TICKS * 0x10           # scale.x += 0x10 per tick from 0
EXP_VELY       = N_TICKS * 0x1800         # velocity.y += 0x1800 per tick from 0
# position.y += velocity.y AFTER velocity.y += 0x1800, both from 0:
#   vel_k = k*0x1800 ; pos = sum_{k=1..N} vel_k = 0x1800 * N(N+1)/2
EXP_POSY       = 0x1800 * (N_TICKS * (N_TICKS + 1) // 2)

# Witness symbol names (sh-none-elf one-underscore convention; map_symbol tolerant).
SYM_TEXT_LO   = "__text_start"
SYM_TEXT_HI   = "__text_end"
SYM_MAGIC     = "_p5_w_magic"
SYM_TICKS     = "_p5_w_ticks"
SYM_DRAWS     = "_p5_w_draw_calls"
SYM_FRAMEID   = "_p5_w_last_frameid"
SYM_CLASSID   = "_p5_w_ring_classid"
SYM_TIMER     = "_p5_w_ring_timer"
SYM_SCALEX    = "_p5_w_ring_scalex"
SYM_VELY      = "_p5_w_ring_vely"
SYM_POSY      = "_p5_w_ring_posy"

WITNESS_SYMS = [SYM_MAGIC, SYM_TICKS, SYM_DRAWS, SYM_FRAMEID, SYM_CLASSID,
                SYM_TIMER, SYM_SCALEX, SYM_VELY, SYM_POSY]

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
    final frameID (the cadence witness the SH-2 build must match)."""
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
    sh-none-elf one-underscore strip on object symbols (see qa_p3_core_boots)."""
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
        "W7 engine DRAW list reached Ring_Draw_Normal->DrawSprite (==N)")
    cmp(SYM_FRAMEID, EXP_FRAMEID,
        "W8 RSDK.ProcessAnimation cadence matches model (frameID==%d)" % EXP_FRAMEID)

    ok = all(c for _, c, _ in checks)
    return ok, checks


def _print_checks(checks):
    for name, ok, detail in checks:
        print("  [%s] %s" % ("GREEN" if ok else " RED ", name))
        print("          %s" % detail)


def run_selftest():
    print("=" * 72)
    print("P5 RING GATE -- SELFTEST (prove the RED path fires)")
    print("=" * 72)
    print("  model: N=%d speed=0x%X dur=0x%X frames=%d loop=%d -> frameID=%d"
          % (N_TICKS, ANIM_SPEED, ANIM_FRAME_DURATION, ANIM_FRAME_COUNT,
             ANIM_LOOP_INDEX, EXP_FRAMEID))
    print("  expect: ticks=%d draws=%d classid=%d timer=%d scalex=%d vely=%d posy=%d"
          % (EXP_TICKS, EXP_DRAW_CALLS, EXP_CLASSID, EXP_TIMER, EXP_SCALEX,
             EXP_VELY, EXP_POSY))
    print("-" * 72)
    # A broken capture: PC in boot ROM, every witness zero (nothing ran).
    bad_vals = {s: 0 for s in WITNESS_SYMS}
    bad_ok, bad_checks = evaluate(0x00000A00, TEXT_LOAD, 0x06030000, bad_vals)
    _print_checks(bad_checks)
    # A synthetic good capture: PC in .text, every witness at its expected value.
    good_vals = {
        SYM_TICKS: EXP_TICKS, SYM_DRAWS: EXP_DRAW_CALLS, SYM_CLASSID: EXP_CLASSID,
        SYM_TIMER: EXP_TIMER, SYM_SCALEX: EXP_SCALEX, SYM_VELY: EXP_VELY,
        SYM_POSY: EXP_POSY, SYM_FRAMEID: EXP_FRAMEID, SYM_MAGIC: MAGIC_VALUE,
    }
    good_ok, _ = evaluate(0x06004F00, TEXT_LOAD, 0x06030000, good_vals)
    print("-" * 72)
    if (not bad_ok) and good_ok:
        print("RESULT: RED (selftest) -- the broken capture is correctly REJECTED")
        print("        (PC outside .text, all witnesses zero) while a synthetic")
        print("        good capture passes. The W0-W8 RED branch is reachable; the")
        print("        gate distinguishes a real object run from a non-run.")
        return 1
    print("RESULT: ERROR -- selftest logic inconsistent (bad_ok=%s good_ok=%s)"
          % (bad_ok, good_ok))
    return 2


def main(argv):
    if "--selftest" in argv:
        return run_selftest()

    pos = [a for a in argv if not a.startswith("--")]
    mcs = pos[0] if len(pos) >= 1 else MCS_DEFAULT
    mp = pos[1] if len(pos) >= 2 else MAP_DEFAULT

    print("=" * 72)
    print("P5 RING GATE: real decomp Ring runs on the true-ported core (Mednafen)")
    print("=" * 72)
    print("  savestate : %s" % mcs)
    print("  link map  : %s" % mp)
    print("  model     : N=%d -> frameID=%d, timer=%d, scalex=%d, vely=%d, posy=%d"
          % (N_TICKS, EXP_FRAMEID, EXP_TIMER, EXP_SCALEX, EXP_VELY, EXP_POSY))
    print("-" * 72)

    if not os.path.isfile(mp):
        print("RESULT: RED -- P5 link map missing (%s)." % mp)
        print("        The P5 harness (p5_ring.cpp verbatim Ring subset + p5_main.cpp")
        print("        engine driver + witness globals) has not been linked yet.")
        print("        Build it (link_p5.sh), then capture a savestate.")
        return 1
    if not os.path.isfile(mcs):
        print("RESULT: RED -- P5 savestate missing (%s)." % mcs)
        print("        Build the P5 ISO and capture a post-loop state:")
        print("        pwsh -File tools/qa_savestate.ps1 -Cue <p5>.cue \\")
        print("             -SaveFrame 12 -Out tools/_portspike/_p5/_p5_ring.mcs")
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
        print("RESULT: RED -- witness symbol(s) absent from the P5 map:")
        for s in missing:
            print("          %s" % s)
        print("        The harness witnesses / two-bank __text_start/__text_end are")
        print("        not wired into the P5 link. Add them, re-link, re-capture.")
        return 1

    print("[1/3] Witness symbols resolved from the link map:")
    for s in (SYM_TEXT_LO, SYM_TEXT_HI, *WITNESS_SYMS):
        print("        %-20s %s" % (s, _hx(syms[s])))

    mod = load_harness()
    sections = mod.parse_savestate(_as_path(mcs))

    raw_magic = mod._peek_bytes(sections, syms[SYM_MAGIC], 4)
    label, perm = calibrate(raw_magic)
    print("[2/3] WRAM byte-order calibration from _p5_w_magic:")
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

    signed = {SYM_SCALEX, SYM_VELY, SYM_POSY, SYM_TIMER, SYM_FRAMEID,
              SYM_TICKS, SYM_DRAWS, SYM_CLASSID}
    vals = {}
    for s in WITNESS_SYMS:
        vals[s] = peek_u32(mod, sections, syms[s], perm, signed=(s in signed))

    print("        peeked witnesses:")
    for s in WITNESS_SYMS:
        print("          %-20s = %s" % (s, _dv(vals[s])))

    ok, checks = evaluate(pc, syms[SYM_TEXT_LO], syms[SYM_TEXT_HI], vals)

    print("[3/3] Object-run witnesses:")
    print("-" * 72)
    _print_checks(checks)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- a REAL decomp object runs on the true-ported core:")
        print("        the engine REGISTERED, SPAWNED and DROVE a Ring; its verbatim")
        print("        Ring_State_LostFX body executed N times (timer/scale/vel/pos")
        print("        all match), Ring_Draw_Normal reached DrawSprite each tick, and")
        print("        RSDK.ProcessAnimation advanced the animator at decomp cadence")
        print("        (frameID==%d). P5 done -- STOP for the P6 GO/NO-GO checkpoint."
              % EXP_FRAMEID)
        return 0
    print("RESULT: RED -- the captured state does not satisfy all object-run")
    print("        witnesses; the real Ring did not run end-to-end as modelled.")
    return 1


def _as_path(p):
    try:
        from pathlib import Path
        return Path(p)
    except Exception:
        return p


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
