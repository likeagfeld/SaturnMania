#!/usr/bin/env python3
# =============================================================================
# qa_p6_ghz_regression.py -- WHOLE-LEVEL GHZ1 regression gate (BINDING).
#
# WHY (user-mandated 2026-06-17): registering Spring/PlaneSwitch silently broke
# the WORKING bridges -- their LoadSpriteAnimation overflowed the shared
# DATASET_STG anim pool, so Bridge.bin's anim alloc failed (brg_frames=0). I had
# verified only the NEW object, not the whole level. This gate re-validates the
# UNION of confirmed GHZ1 features in ONE capture, so any object-add that
# regresses a prior feature is caught BEFORE hand-off. Run it every build.
# See memory/whole-level-regression-gate-every-object-add.md.
#
# Asserts (all must hold in a live GHZ capture):
#   R0  cont_frames   > 0    -- boot healthy (no anim/band hang)
#   R1  brg_classid   > 0    -- Bridge registered
#   R2  brg_frames    > 0    -- Bridge.bin anim LOADED (0 == pool starved == the
#                              regression that vanished the planks)
#   R3  loop_pscount  > 0    -- PlaneSwitch instantiated (loop)
#   R4  0<=rings<100         -- sane ring count. WAS ==0 (valid only while GHZ
#                              rings never rendered, so Sonic couldn't collect any);
#                              #258 armed collectible rings -> the autorun collects
#                              some by frame 130, so ==0 is now stale. The bound
#                              still guards the debug 100-ring carry-over it protected.
#   R5  live_shield  == 0    -- P0 spawn (no fire shield)
#   R6  time_enabled == 1    -- P0 timer ticking
#
#   python tools/_portspike/qa_p6_ghz_regression.py            # boot+capture
#   python tools/_portspike/qa_p6_ghz_regression.py --mcs X.mcs
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

# Shipping GHZ load is host/IO-bound (~24s+ emulated; FpsScale does NOT compress
# it) -- the auto-capture must land AFTER the scene is live or every witness reads
# its init value (false RED). 130 is the proven-good shipping frame (see memory
# shipping-ghz-load-timing.md: cont_frames==0 == captured too early, NOT a freeze).
GATE_FRAME = 130.0
ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_p6_ghzreg.mcs")

NAMES = ["_p6_w_cont_frames", "_p6_w_brg_classid", "_p6_w_brg_frames",
         "_p6_w_loop_pscount", "_p6_w_plr_live_rings", "_p6_w_plr_live_shield",
         "_p6_w_time_enabled", "_p6_w_spring_classid", "_p6_w_spring_frames",
         "_p6_w_spikelog_classid", "_p6_w_spikelog_frames",
         # Range-independent anim-load status (Object->aniFrames; -1==FAILED). This
         # is the careless-proof per-object signal: it does NOT depend on a live
         # entity being near the camera, unlike the *_frames witnesses above.
         "_p6_w_spring_aniframes", "_p6_w_brg_aniframes", "_p6_w_spikelog_aniframes",
         # Global anim-load diagnostics -- re-rooted into shipping so any -1 load
         # is pinpointed on the FIRST capture (no forensic dig, no second build).
         "_p6_saturn_anim_allocfail", "_p6_w_anim_lastfail", "_p6_w_stg_at_fail",
         # BADNIK-VIS (2026-06-18): the VDP1 bind-table demand-vs-capacity gate. The
         # GHZ/Objects.gif surface (badniks + Bridge + SpikeLog all blit it) must be
         # BOUND (handle>=0); the bind DEMAND must not exceed the P6_VDP1_NSHEETS
         # table (else the last surface(s) bind -1 -> invisible, the #181 class that
         # re-bit when Batch 2's Explosions/Animals consumed extra bind slots).
         "_p6_w_ghzobj_surf_handle", "_p6_w_bind_demand", "_p6_w_bind_count",
         "_p6_w_bd_found", "_p6_w_bd_handle"]


def capture(out):
    try:
        os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError:
        pass
    r = subprocess.run(["pwsh", SAVESTATE, "-Cue", "game.cue",
                        "-SaveFrame", str(GATE_FRAME), "-Out", out],
                       capture_output=True, text=True)
    sys.stdout.write(r.stdout[-200:] if r.stdout else "")
    return os.path.exists(out)


def main(argv):
    mcs = argv[argv.index("--mcs") + 1] if "--mcs" in argv else TMP_MCS
    if "--mcs" not in argv and not capture(mcs):
        print("FAIL: no savestate"); return 1

    mod = Q.load_harness()
    mp = Q.read_text(Q.MAP_DEFAULT)
    sec = mod.parse_savestate(Q._as_path(mcs))
    ma = Q.map_symbol(mp, "_p6_w_magic")
    _, perm = Q.calibrate(mod._peek_bytes(sec, ma, 4) if ma else None)
    if perm is None:
        print("RED: magic uncalibrated"); return 1
    v = {n: (Q.peek_u32(mod, sec, Q.map_symbol(mp, n), perm, signed=True)
             if Q.map_symbol(mp, n) else None) for n in NAMES}

    checks = [
        ("R0 boot healthy (cont_frames>0)",      v["_p6_w_cont_frames"],   lambda x: x and x > 0),
        ("R1 Bridge registered (classid>0)",     v["_p6_w_brg_classid"],   lambda x: x and x > 0),
        ("R2 Bridge.bin LOADED (frames>0)",      v["_p6_w_brg_frames"],    lambda x: x and x > 0),
        ("R3 PlaneSwitch live (pscount>0)",      v["_p6_w_loop_pscount"],  lambda x: x and x > 0),
        ("R4 ring count sane (0<=rings<100; #258 collectible)", v["_p6_w_plr_live_rings"], lambda x: x is not None and 0 <= x < 100),
        ("R5 spawn shield==0",                   v["_p6_w_plr_live_shield"],lambda x: x == 0),
        ("R6 timer enabled==1",                  v["_p6_w_time_enabled"],   lambda x: x == 1),
        # #254 anim-pool funding: Spring is the first swept object; once it loads it
        # JOINS the confirmed-feature union so the NEXT object-add can't starve it.
        ("R7 Spring registered (classid>0)",     v["_p6_w_spring_classid"], lambda x: x and x > 0),
        ("R8 Spring.bin LOADED (frames>0)",      v["_p6_w_spring_frames"],  lambda x: x and x > 0),
        # O3 step 1: SpikeLog joins the overlay + the confirmed-feature union.
        ("R9 SpikeLog registered (classid>0)",   v["_p6_w_spikelog_classid"], lambda x: x and x > 0),
        # R10-R12: range-independent anim-load status off <Obj>->aniFrames. A valid
        # spriteAnimationList slot is in [0, 0x400); -1 (0xFFFF) == LoadSpriteAnimation
        # FAILED -> the sprite has no frames -> it draws nothing (the SpikeLog bug).
        # This is the careless-proof per-object gate: it fires RED even when no entity
        # is near the camera (which the *_frames witnesses cannot detect).
        ("R10 SpikeLog anim LOADED (aniFrames>=0)", v["_p6_w_spikelog_aniframes"], lambda x: x is not None and 0 <= x < 0x400),
        ("R11 Spring anim LOADED (aniFrames>=0)",   v["_p6_w_spring_aniframes"],   lambda x: x is not None and 0 <= x < 0x400),
        ("R12 Bridge anim LOADED (aniFrames>=0)",   v["_p6_w_brg_aniframes"],      lambda x: x is not None and 0 <= x < 0x400),
        # R13: no STG-pool anim refusals anywhere in the scene load (the funding net).
        ("R13 no anim alloc-fails (==0)",           v["_p6_saturn_anim_allocfail"],lambda x: x == 0),
        # R14-R16 BADNIK-VIS: the VDP1 bind-table demand-vs-capacity gate. The
        # GHZ/Objects.gif surface (every badnik + Bridge + SpikeLog blits it) must be
        # BOUND, and the total bind demand must fit the P6_VDP1_NSHEETS table -- else
        # the overflowing surface(s) get handle -1 and draw NOTHING (the #181 class).
        ("R14 GHZ/Objects.gif surface BOUND (handle>=0)", v["_p6_w_ghzobj_surf_handle"], lambda x: x is not None and x >= 0),
        ("R15 VDP1 bind demand all bound (count==demand)", (v["_p6_w_bind_count"], v["_p6_w_bind_demand"]),
            lambda t: t[0] is not None and t[1] is not None and t[0] == t[1] and t[1] > 0),
        # R16: a live badnik's current frame resolves to a BOUND handle (the on-screen
        # render contract). bd_found>0 == badniks exist; bd_handle>=0 == its sheet binds.
        ("R16 live badnik frame handle BOUND (>=0)", (v["_p6_w_bd_found"], v["_p6_w_bd_handle"]),
            lambda t: t[0] is not None and t[0] > 0 and t[1] is not None and t[1] >= 0),
    ]
    # Surface the global anim-load diagnostics so a RED above is pinpointed inline
    # (no forensic dig). lastfail = (sprfile_id<<16)|frameCount (bit15: animCount fail);
    # stg_at_fail = DATASET_STG usedStorage at the refusal.
    diag = ("anim_lastfail=0x%X  stg_at_fail=%s" %
            ((v.get("_p6_w_anim_lastfail") or 0) & 0xFFFFFFFF, v.get("_p6_w_stg_at_fail")))
    print("=" * 64)
    print("GHZ1 WHOLE-LEVEL REGRESSION GATE")
    print("=" * 64)
    ok = True
    for label, val, test in checks:
        passed = False
        try:
            passed = bool(test(val))
        except Exception:
            passed = False
        ok = ok and passed
        print("  [%s] %-38s = %s" % ("GREEN" if passed else " RED ", label, Q._dv(val)))
    print("  diag: " + diag)
    print("-" * 64)
    if ok:
        print("RESULT: GREEN -- all confirmed GHZ1 features intact")
        return 0
    print("RESULT: RED -- a confirmed feature REGRESSED (see the RED line above)")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
