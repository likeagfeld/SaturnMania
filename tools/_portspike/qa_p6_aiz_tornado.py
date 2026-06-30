#!/usr/bin/env python3
# =============================================================================
# qa_p6_aiz_tornado.py -- R3.1 RED-first gate: the AIZ intro Tornado biplane
# actually RENDERS (its sprite sheet binds to VDP1), not merely flies invisibly.
#
# MEASURED ROOT CAUSE (M3.2 confirmed the Tornado entity is registered + FLYING --
# tornado x 60->14282 -- but it draws NOTHING). The AIZTornado object's StageLoad
# does RSDK.LoadSpriteAnimation("AIZ/AIZTornado.bin"), whose frames live in
# AIZ/Objects.gif (the biplane body+propeller+flame+pilot -- parsed: anim0 116x64
# body speed=0, anim1 6x44 propeller, anim2 58x36 flame, anim3/6 ~25x20 pilot).
# AIZ/Objects.gif IS in Data.rsdk (filelist:1164), so LoadSpriteAnimation parses
# fine + creates a gfxSurface with hash("AIZ/Objects.gif") -- BUT its saturnSheetSlot
# stays -1 because NO AIZOBJ.SHT is staged. -> no VDP1 handle -> every
# AIZTornado_Draw DrawSprite (AIZTornado.c:29-42) drops -> the biplane is invisible.
# IDENTICAL class to CP4b LOGOS.SHT / CP5b.1 TLOGO.SHT / CP5b.2 TSONIC.SHT (all
# "surface saturnSheetSlot==-1, no .SHT staged -> handle<0 -> blits dropped").
#
# FIX (R3.1, all P6_AIZ_TEST-gated, GHZ + plain-menu byte-identical):
#   1. build_sheet_bands.py: emit cd/AIZOBJ.SHT from AIZ/Objects.gif (P6_AIZ_TEST guard).
#   2. p6_io_main.cpp p6_aiz_reload: stage AIZOBJ.SHT (load->Stage->SetHash("AIZ/
#      Objects.gif")->MakeResident), mirroring the LOGOS/TLOGO block.
#   3. p6_io_main.cpp: AIZ/Objects.gif sheet-bind witnesses (mirror the GHZ/Objects.gif
#      diag at :2808-2837).
#   4. p6_ovl_ghz.c: AIZTornado animator frame-count witness.
#
# Measurable conditions (all must hold) in a P6_AIZ_TEST capture (~SaveFrame 95 --
# the AIZ load is ~50 s emulated-CD-IO-bound #251, then the cutscene ticks):
#   C0 _p6_w_cont_frames        > 0  -- the engine booted past load (NOT the #228 blue-
#                                       screen trap: WRAM-H over the front-end heap limit).
#   C1 _p6_w_aizobj_slot       >= 0  -- AIZOBJ.SHT staged + hashed "AIZ/Objects.gif"
#                                       (the SaturnSheet slot p6_stage_sheet_hash returned).
#   C2 _p6_w_aiz_tornado_frames > 0  -- SetSpriteAnimation resolved the sheet -> the
#                                       Tornado animator has frames (Draw emits rects).
#
# The full VDP1 surface->handle scan was DROPPED (the front-end build is WRAM-H-tight --
# the surface-scan diag pushed _end past the heap limit -> #228 boot trap, MEASURED blue-
# screen). The actual VDP1 bind / on-screen render is the SCREENSHOT biplane-red measure.
#
# RED on the CURRENT build: these witnesses are ABSENT from game.map (no AIZOBJ stage)
# -> "witnesses absent". GREEN once R3.1 lands. SCREENSHOT the AIZ fly-in too -- a GREEN
# gate with no visible biplane is the proxy trap (the user verifies the red plane +
# propeller + pilot on screen, NOT just the staged slot).
#
#   python tools/_portspike/qa_p6_aiz_tornado.py             # boot (P6_AIZ_TEST) + capture + verdict
#   python tools/_portspike/qa_p6_aiz_tornado.py --mcs X.mcs # evaluate an existing capture
#   python tools/_portspike/qa_p6_aiz_tornado.py --static    # map-only RED check (no capture)
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

# AIZ shipping load is emulated-CD-IO-bound at ~50 s (#251). qa_p6_aiz_cutscene uses
# SaveFrame 85 for the cutscene-state latch; the Tornado StageLoad + the bind loop run
# during the same synchronous p6_aiz_reload, so they latch by then too. Capture a touch
# DEEPER (95) so several cutscene frames have advanced the propeller/flame ProcessAnimation.
GATE_FRAME = 95.0

ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_p6_aiz_tornado.mcs")

# R3.1 witnesses (DEFINED in p6_io_main.cpp under P6_AIZ_TEST + p6_ovl_ghz.c; -u-rooted
# in build_p6scene_objs.sh so LTO keeps them). MINIMAL set -- the front-end build is
# WRAM-H-tight (#228), so the full surface-scan diag was dropped; the SCREENSHOT biplane-
# red pixel measure is the binding render proof (run qa_boot + the analyzer separately).
NAMES = ["_p6_w_aizobj_slot", "_p6_w_aiz_tornado_frames",
         "_p6_w_aiz_tornado_classid", "_p6_w_frontend_folder_tag", "_p6_w_cont_frames",
         # R3.4 (#306 follow-on): the post-fly-in cutscene actor anim-load latches.
         "_p6_w_aiz_claw_aniframes", "_p6_w_aiz_eggrobo_aniframes"]


# R3.3 (Task #306) CRAM-bank collision gate. The AIZ Tornado is a VDP1 8-bpp sprite;
# it reads the sprite palette from CRAM bank 1 (p6_vdp1.c p6_pal_mirror writes
# 0x05F00200 + i*2). The VDP2 FG NBG reads bank 0 (0x05F00000 + i*2). BOTH are uploaded
# from the SAME engine fullPalette[0], so for every index they MUST be byte-identical.
# MEASURED bug: the R2.x AIZ BG setup (p6_vdp2.c) wrote its 3 jungle CRAM banks at
# PAL_BASE=16 -> CRAM entries 256..303 == bank 1 sprite-idx 0..47 -> it STOMPED the
# Tornado's red body indices (16-18) with jungle greens (0xfc1f magenta / 0x8860 green
# vs the global reds 0x8008/0x8012/0x801C in bank 0). The fix relocates the BG banks to
# PAL_BASE=32 (CRAM 512..559, clear of bank 1). GREEN == bank1 matches bank0 over the
# clobbered window. This is the savestate-PRIMARY measure (the biplane pixel-red is the
# secondary screenshot confirm).
CRAM_BANK0 = 0x05F00000   # VDP2 FG NBG palette (engine fullPalette[0])
CRAM_BANK1 = 0x05F00200   # VDP1 sprite palette (p6_pal_mirror of fullPalette[0])
CRAM_CHECK_LO = 16        # sprite idx 16..47 = the Tornado body reds + detail (row 1-2),
CRAM_CHECK_HI = 47        # exactly the entries the BG banks 16-18 clobbered.


def cram16(mod, sec, addr):
    b = mod._peek_bytes(sec, addr, 2)
    return int.from_bytes(b, "big") if b else None


def cram_bank_match(mod, sec):
    """Return (n_mismatch, first_mismatch_tuple_or_None) over the clobbered window."""
    mism = 0
    first = None
    for i in range(CRAM_CHECK_LO, CRAM_CHECK_HI + 1):
        b0 = cram16(mod, sec, CRAM_BANK0 + i * 2)
        b1 = cram16(mod, sec, CRAM_BANK1 + i * 2)
        if b0 is None or b1 is None:
            return (-1, ("read-fail", i))
        if b0 != b1:
            mism += 1
            if first is None:
                first = (i, b0, b1)
    return (mism, first)


def capture(out):
    try:
        os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError:
        pass
    env = dict(os.environ, P6_AIZ_TEST="1")
    r = subprocess.run(
        ["pwsh", SAVESTATE, "-Cue", "game.cue", "-SaveFrame", str(GATE_FRAME),
         "-Out", out],
        capture_output=True, text=True, env=env)
    sys.stdout.write(r.stdout[-400:] if r.stdout else "")
    sys.stdout.write(r.stderr[-400:] if r.stderr else "")
    return os.path.exists(out)


def main(argv):
    mp = Q.read_text(Q.MAP_DEFAULT)
    present = {n: (Q.map_symbol(mp, n) is not None) for n in NAMES}

    if "--static" in argv or not all(present.values()):
        print("=" * 70)
        print("R3.1 AIZ TORNADO -- the biplane sprite sheet binds to VDP1 (renders)")
        print("=" * 70)
        for n in NAMES:
            print("  [%s] witness %s" % ("present" if present[n] else "ABSENT ", n))
        missing = [n for n in NAMES if not present[n]]
        if missing:
            print("-" * 70)
            print("RESULT: RED -- %d/%d R3.1 witnesses ABSENT from game.map. AIZOBJ.SHT "
                  "staging + the aizobj sheet-bind witnesses are NOT implemented yet. "
                  "This is the expected RED baseline for R3.1."
                  % (len(missing), len(NAMES)))
            return 1
        if "--static" in argv:
            print("RESULT: GREEN(static) -- all R3.1 witnesses present in the map (run "
                  "without --static to force the AIZ load + verify C1-C5).")
            return 0

    mcs = argv[argv.index("--mcs") + 1] if "--mcs" in argv else TMP_MCS
    if "--mcs" not in argv and not capture(mcs):
        print("FAIL: no savestate"); return 1
    mod = Q.load_harness()
    sec = mod.parse_savestate(Q._as_path(mcs))
    ma = Q.map_symbol(mp, "_p6_w_magic")
    _, perm = Q.calibrate(mod._peek_bytes(sec, ma, 4) if ma else None)
    if perm is None:
        print("RED: magic uncalibrated"); return 1
    v = {n: Q.peek_u32(mod, sec, Q.map_symbol(mp, n), perm, signed=True) for n in NAMES}

    cram_mism, cram_first = cram_bank_match(mod, sec)
    checks = [
        ("C0 engine booted past load (cont_frames>0 -- not the #228 blue-screen trap)",
         v["_p6_w_cont_frames"], lambda x: x is not None and x > 0),
        ("C1 AIZOBJ.SHT staged+hashed (aizobj_slot>=0)",
         v["_p6_w_aizobj_slot"], lambda x: x is not None and x >= 0),
        ("C2 Tornado animator resolved the sheet (tornado_frames>0)",
         v["_p6_w_aiz_tornado_frames"], lambda x: x is not None and x > 0),
        ("C3 VDP1 sprite pal == FG pal over idx 16-47 (no AIZ-BG CRAM-bank stomp)",
         cram_mism, lambda x: x == 0),
        ("C4 KingClaw anim loaded (AIZ/Claw.bin in AIZOBJ.PAK, claw_aniframes>=0)",
         v["_p6_w_aiz_claw_aniframes"], lambda x: x is not None and x >= 0),
        ("C5 EggRobo anim loaded (AIZ/AIZEggRobo.bin in AIZOBJ.PAK, eggrobo_aniframes>=0)",
         v["_p6_w_aiz_eggrobo_aniframes"], lambda x: x is not None and x >= 0),
    ]
    print("=" * 70)
    print("R3.1 AIZ TORNADO -- the biplane sprite sheet stages + the animator resolves")
    print("=" * 70)
    print("  context: folder_tag=%s (expect 0x4149 'AI'); tornado_cid=%s"
          % (hex(v["_p6_w_frontend_folder_tag"] & 0xFFFF)
             if isinstance(v["_p6_w_frontend_folder_tag"], int) else v["_p6_w_frontend_folder_tag"],
             Q._dv(v["_p6_w_aiz_tornado_classid"])))
    if cram_first and cram_mism != 0:
        if cram_first[0] == "read-fail":
            print("  CRAM: read-fail at sprite idx %s" % cram_first[1])
        else:
            print("  CRAM: %d/%d mismatch in idx 16-47; first idx %d FG=0x%04x SPR=0x%04x "
                  "(SPR != FG == AIZ-BG bank stomped the sprite palette)"
                  % (cram_mism, CRAM_CHECK_HI - CRAM_CHECK_LO + 1,
                     cram_first[0], cram_first[1], cram_first[2]))
    ok = True
    for label, val, test in checks:
        try:
            passed = bool(test(val))
        except Exception:
            passed = False
        ok = ok and passed
        print("  [%s] %-62s = %s" % ("GREEN" if passed else " RED ", label, Q._dv(val)))
    print("-" * 70)
    if ok:
        print("RESULT: GREEN -- AIZOBJ.SHT is staged + the Tornado animator has frames. "
              "The biplane data path is wired. BINDING render proof is the SCREENSHOT: run "
              "  pwsh tools/qa_boot.ps1 -Cue game.cue -Wait 120 -Every 2 -Shots 8 -Out _aizt.png "
              "and confirm the red biplane (with propeller + Sonic/Tails pilot) on screen. "
              "NEXT = R3.2 pilot anim-index faithfulness + cutscene beats (claw/ruby/warp).")
        return 0
    print("RESULT: RED -- see the RED check. C0 RED = #228 boot trap (WRAM-H over the "
          "front-end heap limit -- reclaim before adding; MEASURED blue-screen). C1 RED = "
          "AIZOBJ.SHT not staged (p6_stage_sheet_hash returned -1: load/Stage failed). "
          "C2 RED = AIZTornado_StageLoad's LoadSpriteAnimation found no frames (sheet "
          "hash mismatch, or Tornado not registered / StarPost deref).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
