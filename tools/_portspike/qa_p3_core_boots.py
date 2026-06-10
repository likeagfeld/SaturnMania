#!/usr/bin/env python3
# P3 boot gate (Task #202) -- prove the TRUE-PORTED RSDKv5 logic core actually
# BOOTS on the Saturn (Mednafen) and transitions THROUGH ENGINESTATE_LOAD.
#
# WHAT P3 PROVES (the engine-true-port pivot's first runtime milestone)
#   P1 = the 16 logic-core TUs codegen to SH-2 objects.
#   P2 = they LINK clean against newlib + libgcc (0 unresolved) and the linked
#        CODE fits the Saturn code budget.
#   P4 = the static .bss is retargeted under the 2 MB physical line (gated levers).
#   P3 (this) = the linked image, placed by the two-bank linker script, BOOTS:
#        crt0 -> main -> RunRetroEngine(0,NULL) -> InitStorage (heap) ->
#        InitUserCore (userCore wired) -> InitEngine -> StartGameObjects
#        (sceneInfo.state = ENGINESTATE_LOAD) -> LoadGameConfig (LoadFile fails,
#        no Data pack on the proof ISO -> sceneInfo.listData stays NULL) ->
#        engine.initialized = true -> RenderDevice::Init() -> frame loop ->
#        ProcessEngine: state==LOAD && listData==NULL -> state = ENGINESTATE_NONE.
#
#   ENGINESTATE_NONE (== 13 under RETRO_REVISION=2: RETRO_REV02 on adds the two
#   ERRORMSG enumerators before NONE, RETRO_REV0U off drops GAME_FINISHED after;
#   verified Scene.hpp:93-111) is the perfect NON-ZERO boot witness. It is the
#   state ProcessEngine (RetroEngine.cpp:350-353) parks in when the config load
#   yields no scene list -- reached ONLY by booting, initializing, AND running
#   one ProcessEngine pass through the ENGINESTATE_LOAD case. ENGINESTATE_LOAD
#   itself == 0, which collides with zero-init .bss, so state==0 cannot witness
#   a boot; state==13 can.
#
# THE THREE RUNTIME WITNESSES (all measured from a Mednafen savestate)
#   W1  SH2-M PC in [__text_start, __text_end)  -- the master SH-2 is executing
#       core code (spinning the RunRetroEngine frame loop), not crashed into the
#       boot ROM / an exception vector / uninitialised .bss.
#   W2  engine.initialized == 1                 -- InitEngine completed.
#   W3  sceneInfo.state == ENGINESTATE_NONE     -- ProcessEngine ran the LOAD
#       case and fell through to NONE; i.e. the engine reached AND PASSED the
#       ENGINESTATE_LOAD milestone the task names.
#
# OFFSET-FREE WITNESS POINTERS (no hand-computed SH-2 struct offsets)
#   A Saturn-gated witness TU (platform/Saturn/P3Witness_Saturn.cpp) exports
#   C-linkage CONSTANTS whose initialisers the LINKER resolves:
#       _p3_w_engine_init  = &RSDK::engine.initialized   (bool32*, 4 B)
#       _p3_w_scene_state  = &RSDK::sceneInfo.state       (uint8*,  1 B)
#       _p3_w_expect_none  = (uint32)ENGINESTATE_NONE     (the compiler bakes 13)
#       _p3_w_magic        = 0x12345678                   (byte-order anchor)
#   The gate reads each pointer's VALUE from its .map symbol address (a double
#   indirection: peek the pointer word -> get the runtime field address -> peek
#   the field). The compiler computes &engine.initialized / &sceneInfo.state for
#   us, so the gate never needs offsetof() of an RSDK struct. _p3_w_expect_none
#   lets the gate compare against the BUILD's own ENGINESTATE_NONE rather than a
#   hard-coded 13 (catches a mis-set RETRO_REVISION).
#
# WRAM-H BYTE-ORDER SELF-CALIBRATION (defuses the mcs_extract WRAM-H pair-swap
# caveat, Task #136). _p3_w_magic is the link-time constant 0x12345678 -> Saturn
# big-endian bytes 12 34 56 78. The gate peeks the magic's 4 raw bytes and finds
# which permutation (identity / 16-bit pair-swap / full LE / 32-bit word-swap)
# the WRAM-H chunk underwent in THIS savestate, then inverts that same
# permutation on every other WRAM-H 32-bit read. The witness pointer constants,
# the values they point at (RSDK::engine + RSDK::sceneInfo are kept in WRAM-H by
# the two-bank script), and the magic all share one calibration. No assumption
# about whether the chunk is swapped -- it is MEASURED per state.
#
# RED-first (CLAUDE.md 4.7 / skill Step 7): on the current tree there is NO P3
# link map and NO P3 savestate, so the gate fires RED ("artifacts missing").
# It also fires RED if the witness symbols are absent from the map (witness TU /
# linker script not wired), if the SH-2 PC is outside core .text (crash / never
# entered the core), if engine.initialized != 1 (InitEngine never ran), or if
# sceneInfo.state != ENGINESTATE_NONE (never passed through LOAD). It turns GREEN
# only when a real captured boot satisfies W1+W2+W3.
#
#   python tools/_portspike/qa_p3_core_boots.py [savestate.mcs] [link.map]
#   python tools/_portspike/qa_p3_core_boots.py --selftest   # prove RED fires
#
# P6 RESTORATION: the witness TU, the two-bank linker script, crt0.s and this
# gate are P3 scaffolding. They are Saturn-only and #if'd / build-gated, so the
# PC build is untouched. P6 decides whether the true-port becomes the shipping
# path; if so this scaffolding stays as the Saturn entry, else it is removed
# alongside src/rsdk + src/mania exactly as the pivot plan states.

import importlib.util
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# link_p3.sh emits the two-bank link map + boot image under the _p3/ subdir;
# the boot savestate is captured next to them.
MAP_DEFAULT = os.path.join(HERE, "_p3", "_p3.map")
MCS_DEFAULT = os.path.join(HERE, "_p3", "_p3_boot.mcs")
MCS_EXTRACT = os.path.normpath(os.path.join(HERE, "..", "mcs_extract.py"))

# The IP.BIN load address / SGL ___Start (verified from the COMMON IP.BIN dump,
# offset 0x0F0 == 06 00 40 00). The two-bank script places .text here.
TEXT_LOAD = 0x06004000
WRAM_H = (0x06000000, 0x06100000)
WRAM_L = (0x00200000, 0x00300000)

# Witness symbols the two-bank P3 link must export into _p3_link.map.
SYM_TEXT_LO = "__text_start"
SYM_TEXT_HI = "__text_end"
SYM_W_INIT = "_p3_w_engine_init"
SYM_W_STATE = "_p3_w_scene_state"
SYM_W_NONE = "_p3_w_expect_none"
SYM_W_MAGIC = "_p3_w_magic"

# ENGINESTATE_NONE under RETRO_REVISION=2 (documented derivation; the build's own
# value is also read back via _p3_w_expect_none and cross-checked).
ENGINESTATE_NONE_REV02 = 13

# Byte-order anchor. 0x12345678 maps to four DISTINCT byte strings under each of
# the four plausible WRAM-H chunk permutations, so a single 4-byte read uniquely
# identifies the transform.
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
    """Address of a defined symbol from GNU ld -Map output. A defined symbol
    sits on its own indented line: '   0x<addr>   <name>' (optionally ' = .').

    UNDERSCORE TOLERANCE: sh-none-elf prepends one underscore at the ELF level
    (C `p3_w_magic` -> ELF `_p3_w_magic`), but GNU ld's -Map output STRIPS one
    leading underscore from object-defined symbols when it prints them, so the
    same symbol shows as `p3_w_magic`. Linker-SCRIPT-defined symbols
    (`__text_start = .`) print verbatim with every underscore. To match either
    display form, strip one optional leading underscore from the search name and
    allow one optional leading underscore in the map text."""
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
    """Return (label, perm) such that applying perm to MAGIC_BE yields the bytes
    actually captured for the magic word, or (None, None) if no match."""
    if raw_magic is None or len(raw_magic) < 4:
        return None, None
    raw4 = bytes(raw_magic[:4])
    for label, perm in PERMS:
        if bytes(MAGIC_BE[i] for i in perm) == raw4:
            return label, perm
    return None, None


def decode_u32(raw4, perm):
    """raw4 == perm(true_be); recover the true big-endian word and unpack it."""
    inv = [perm.index(j) for j in range(4)]
    true_be = bytes(raw4[inv[j]] for j in range(4))
    return struct.unpack(">I", true_be)[0]


def peek_u32(mod, sections, addr, perm):
    raw = mod._peek_bytes(sections, addr, 4)
    if raw is None or len(raw) < 4:
        return None
    return decode_u32(bytes(raw[:4]), perm)


def peek_u8(mod, sections, addr, perm):
    """A single byte in a possibly-pair-swapped chunk must be recovered via its
    aligned 32-bit word: read the word, calibrate to the true big-endian word,
    then select the byte by its address-mod-4 position."""
    word = peek_u32(mod, sections, addr & ~3, perm)
    if word is None:
        return None
    shift = 8 * (3 - (addr & 3))
    return (word >> shift) & 0xFF


def in_range(addr, rng):
    return addr is not None and rng[0] <= addr < rng[1]


def evaluate(pc, text_lo, text_hi, init_val, state_val, expect_none):
    """Pure W1/W2/W3 verdict (shared by --selftest and the real path)."""
    checks = []
    c1 = pc is not None and text_lo is not None and text_hi is not None \
        and text_lo <= pc < text_hi
    checks.append(("W1 SH2-M PC in core .text [%s, %s)"
                   % (_hx(text_lo), _hx(text_hi)), c1,
                   "PC=%s" % _hx(pc)))
    c2 = init_val == 1
    checks.append(("W2 engine.initialized == 1 (InitEngine ran)", c2,
                   "engine.initialized=%s" % _dv(init_val)))
    c3 = expect_none == ENGINESTATE_NONE_REV02
    checks.append(("W3a witness ENGINESTATE_NONE == 13 (REV02 enum sanity)", c3,
                   "compiled ENGINESTATE_NONE=%s" % _dv(expect_none)))
    c4 = state_val is not None and expect_none is not None \
        and state_val == expect_none
    checks.append(("W3b sceneInfo.state == ENGINESTATE_NONE (passed THROUGH LOAD)",
                   c4, "sceneInfo.state=%s expect=%s"
                   % (_dv(state_val), _dv(expect_none))))
    return (c1 and c2 and c3 and c4), checks


def _hx(v):
    return "None" if v is None else ("0x%08X" % v)


def _dv(v):
    return "None" if v is None else str(v)


def run_selftest():
    print("=" * 72)
    print("P3 BOOT GATE -- SELFTEST (prove the RED path fires)")
    print("=" * 72)
    # A deliberately-broken capture: PC parked in the boot ROM, engine never
    # initialised, state still zero-init ENGINESTATE_LOAD. The gate MUST reject.
    bad_ok, bad_checks = evaluate(pc=0x00000A00, text_lo=TEXT_LOAD,
                                  text_hi=0x06030000, init_val=0,
                                  state_val=0, expect_none=13)
    for name, ok, detail in bad_checks:
        print("  [%s] %s" % ("GREEN" if ok else " RED ", name))
        print("          %s" % detail)
    # A synthetic good capture: PC in .text, initialised, state==NONE.
    good_ok, _ = evaluate(pc=0x06004F00, text_lo=TEXT_LOAD, text_hi=0x06030000,
                          init_val=1, state_val=13, expect_none=13)
    print("-" * 72)
    if (not bad_ok) and good_ok:
        print("RESULT: RED (selftest) -- the broken capture is correctly REJECTED")
        print("        (PC outside .text, engine.initialized=0, state!=NONE) while")
        print("        a synthetic good capture passes. The W1/W2/W3 RED branch is")
        print("        reachable; the gate can distinguish a boot from a non-boot.")
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
    print("P3 BOOT GATE: true-ported core reaches ENGINESTATE_NONE (Mednafen)")
    print("=" * 72)
    print("  savestate : %s" % mcs)
    print("  link map  : %s" % mp)
    print("-" * 72)

    # --- Artifact presence (the natural RED-first state on the current tree) ---
    if not os.path.isfile(mp):
        print("RESULT: RED -- P3 link map missing (%s)." % mp)
        print("        The two-bank P3 link has not been produced yet. Build the")
        print("        P3 boot image (real main + SaturnUserCore + _sbrk heap +")
        print("        witness TU + two-bank sgl-fork + crt0) emitting _p3_link.map,")
        print("        then capture a boot savestate.")
        return 1
    if not os.path.isfile(mcs):
        print("RESULT: RED -- P3 boot savestate missing (%s)." % mcs)
        print("        Build the P3 ISO and capture a deep-frame state:")
        print("        pwsh -File tools/qa_savestate.ps1 -Cue <p3>.cue \\")
        print("             -SaveFrame 18 -Out tools/_portspike/_p3_boot.mcs")
        return 1

    map_text = read_text(mp)

    # --- Witness symbol resolution from the .map -------------------------------
    syms = {}
    missing = []
    for s in (SYM_TEXT_LO, SYM_TEXT_HI, SYM_W_INIT, SYM_W_STATE, SYM_W_NONE,
              SYM_W_MAGIC):
        a = map_symbol(map_text, s)
        syms[s] = a
        if a is None:
            missing.append(s)
    if missing:
        print("RESULT: RED -- witness symbol(s) absent from the P3 map:")
        for s in missing:
            print("          %s" % s)
        print("        The witness TU (P3Witness_Saturn.cpp) and/or the two-bank")
        print("        linker script's __text_start/__text_end are not wired into")
        print("        the P3 link. Add them, re-link, re-capture.")
        return 1

    print("[1/3] Witness symbols resolved from the link map:")
    for s in (SYM_TEXT_LO, SYM_TEXT_HI, SYM_W_INIT, SYM_W_STATE, SYM_W_NONE,
              SYM_W_MAGIC):
        print("        %-20s %s" % (s, _hx(syms[s])))

    # --- Savestate peeks (with WRAM-H byte-order self-calibration) -------------
    mod = load_harness()
    sections = mod.parse_savestate(_as_path(mcs))

    raw_magic = mod._peek_bytes(sections, syms[SYM_W_MAGIC], 4)
    label, perm = calibrate(raw_magic)
    print("[2/3] WRAM-H byte-order calibration from _p3_w_magic:")
    if perm is None:
        print("        magic raw bytes = %s (expected a permutation of %s)"
              % (raw_magic.hex() if raw_magic else "None", MAGIC_BE.hex()))
        print("RESULT: RED -- the magic anchor (0x%08X) did not read back as any"
              % MAGIC_VALUE)
        print("        known byte permutation. Either the image did not load to")
        print("        0x%08X, .rodata is not in WRAM-H, or the witness TU is" % TEXT_LOAD)
        print("        not linked. No reliable peek is possible; treat as no-boot.")
        return 1
    print("        magic raw = %s -> transform: %s" % (raw_magic.hex(), label))

    regs = mod._sh2_regs(sections, "master")
    pc = regs.get("PC") if regs else None

    p_init = peek_u32(mod, sections, syms[SYM_W_INIT], perm)
    p_state = peek_u32(mod, sections, syms[SYM_W_STATE], perm)
    expect_none = peek_u32(mod, sections, syms[SYM_W_NONE], perm)

    # The witness pointers must resolve into WRAM (RSDK::engine + RSDK::sceneInfo
    # are .bss). A pointer outside WRAM means calibration or boot failed.
    if not (in_range(p_init, WRAM_H) or in_range(p_init, WRAM_L)) or \
       not (in_range(p_state, WRAM_H) or in_range(p_state, WRAM_L)):
        print("[2/3] witness pointer sanity:")
        print("        *_p3_w_engine_init = %s" % _hx(p_init))
        print("        *_p3_w_scene_state = %s" % _hx(p_state))
        print("RESULT: RED -- a witness pointer does not resolve into WRAM. The")
        print("        engine globals were not placed / the image did not boot /")
        print("        calibration is wrong. No valid field read; treat as no-boot.")
        return 1

    init_val = peek_u32(mod, sections, p_init, perm)
    state_val = peek_u8(mod, sections, p_state, perm)

    print("        *_p3_w_engine_init -> %s  -> engine.initialized = %s"
          % (_hx(p_init), _dv(init_val)))
    print("        *_p3_w_scene_state -> %s  -> sceneInfo.state    = %s"
          % (_hx(p_state), _dv(state_val)))
    print("        *_p3_w_expect_none -> ENGINESTATE_NONE = %s" % _dv(expect_none))

    ok, checks = evaluate(pc, syms[SYM_TEXT_LO], syms[SYM_TEXT_HI],
                          init_val, state_val, expect_none)

    print("[3/3] Boot witnesses:")
    print("-" * 72)
    for name, c, detail in checks:
        print("  [%s] %s" % ("GREEN" if c else " RED ", name))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the true-ported RSDKv5 logic core BOOTS on Saturn:")
        print("        SH2-M is executing core .text, InitEngine completed")
        print("        (engine.initialized=1), and ProcessEngine ran the")
        print("        ENGINESTATE_LOAD case through to ENGINESTATE_NONE (13).")
        print("        The core reached AND passed the load milestone. P3 done.")
        return 0
    print("RESULT: RED -- the captured state does not satisfy all three boot")
    print("        witnesses; the core did not boot through ENGINESTATE_LOAD.")
    return 1


def _as_path(p):
    try:
        from pathlib import Path
        return Path(p)
    except Exception:
        return p


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
