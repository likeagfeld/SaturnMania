#!/usr/bin/env python3
# GHZ1 parity P0 -- spawn-state gate (RED-first, data-driven).
#
# Hands-on testing of the engine-shipping GHZ build found the player spawns with
# 100 rings + a fire shield and the level timer frozen at 0'00"00 -- none of which
# match GHZ1. Root causes (decomp-cited):
#   * rings/shield: Player_Create reads Player->rings (Player.c:643) and applies
#     Player->powerups as a shield (Player.c:654-656). At a fresh-act start those
#     carry-over statics are 0; the lean engine boot skips the menu/new-game path
#     that zeroes them, so the first GHZ player inherits uninitialized static memory
#     (100 rings / fire shield). The fire shield also BURNS burnable bridges
#     (Bridge_Burn -> destroyEntity), which is why bridges 'disappear'.
#   * timer: ProcessSceneTimer() (Scene.cpp:1388) was never called in p6_ghz_frame
#     (ProcessEngine calls it every gameplay frame). Zone_Create already sets
#     sceneInfo.timeEnabled=true (Zone.c:820/857), so the clock just needed ticking.
#
# This gate captures BOTH the bug and the fix in one shot: newgame_pre_* witness the
# uninitialized static the load-arm inherited (the RED that p6_player_newgame_reset
# corrects), live_* witness the actual SLOT_PLAYER1 player post-spawn (the GREEN).
#
#   S1  p6_w_plr_live_rings   == 0            (player spawns with 0 rings)
#   S2  p6_w_plr_live_shield  == 0            (SHIELD_NONE)
#   S3  p6_w_time_enabled     == 1  AND  p6_w_timer > 0   (clock enabled + ticking)
#
#   python tools/_portspike/qa_p6_spawnstate.py            # boot+capture+verdict
#   python tools/_portspike/qa_p6_spawnstate.py --mcs X.mcs

import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

GATE_FRAME = 50.0
ROOT       = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE  = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS    = os.path.join(HERE, "_p6_spawn.mcs")

NAMES = ["_p6_w_cont_frames", "_p6_w_plr_live_rings", "_p6_w_plr_live_shield",
         "_p6_w_time_enabled", "_p6_w_timer",
         "_p6_w_plr_newgame_pre_rings", "_p6_w_plr_newgame_pre_pwr"]


def capture(out):
    lck = os.path.join(ROOT, ".mednafen", "mednafen.lck")
    try:
        os.remove(lck)
    except OSError:
        pass
    cmd = ["pwsh", SAVESTATE, "-Cue", "game.cue", "-SaveFrame", str(GATE_FRAME), "-Out", out]
    r = subprocess.run(cmd, capture_output=True, text=True)
    sys.stdout.write(r.stdout[-400:] if r.stdout else "")
    if not os.path.exists(out):
        print("FAIL: capture produced no savestate"); return False
    return True


def main(argv):
    mcs = argv[argv.index("--mcs") + 1] if "--mcs" in argv else None
    if mcs is None:
        mcs = TMP_MCS
        if not capture(mcs):
            return 1

    mod = Q.load_harness()
    map_text = Q.read_text(Q.MAP_DEFAULT)
    sections = mod.parse_savestate(Q._as_path(mcs))
    ma = Q.map_symbol(map_text, "_p6_w_magic")
    raw = mod._peek_bytes(sections, ma, 4) if ma else None
    _, perm = Q.calibrate(raw)
    if perm is None:
        print("RED: magic uncalibrated"); return 1

    v = {}
    for n in NAMES:
        a = Q.map_symbol(map_text, n)
        v[n] = Q.peek_u32(mod, sections, a, perm) if a else None

    print("=== qa_p6_spawnstate (GHZ1 parity P0) ===")
    print("  mcs            = %s" % os.path.basename(mcs))
    print("  cont_frames    = %s (GHZ live)" % Q._dv(v["_p6_w_cont_frames"]))
    print("  -- the bug this corrects (uninitialized static at load-arm) --")
    print("  pre_rings      = %s   (was 100 = the wrong static)" % Q._dv(v["_p6_w_plr_newgame_pre_rings"]))
    print("  pre_powerups   = %s   (nonzero = the fire shield seed)" % Q._dv(v["_p6_w_plr_newgame_pre_pwr"]))
    print("  -- the fixed live player + clock --")
    print("  live_rings     = %s   (expect 0)" % Q._dv(v["_p6_w_plr_live_rings"]))
    print("  live_shield    = %s   (expect 0 = SHIELD_NONE)" % Q._dv(v["_p6_w_plr_live_shield"]))
    print("  time_enabled   = %s   (expect 1)" % Q._dv(v["_p6_w_time_enabled"]))
    print("  timer          = %s   (expect > 0 = clock ticking)" % Q._dv(v["_p6_w_timer"]))

    rings = v["_p6_w_plr_live_rings"]; shield = v["_p6_w_plr_live_shield"]
    ten   = v["_p6_w_time_enabled"];   tmr = v["_p6_w_timer"]
    if rings is None or shield is None or ten is None or tmr is None:
        print("RED: spawn-state witnesses absent (build predates P0)"); return 1

    s1 = (rings == 0)
    s2 = (shield == 0)
    s3 = (ten == 1 and tmr > 0)
    print("  [%s] S1 player spawns with 0 rings" % ("GREEN" if s1 else " RED "))
    print("  [%s] S2 no shield at spawn (SHIELD_NONE)" % ("GREEN" if s2 else " RED "))
    print("  [%s] S3 level clock enabled + ticking" % ("GREEN" if s3 else " RED "))
    if s1 and s2 and s3:
        print("RESULT: GREEN -- GHZ1 spawn state matches the original (0 rings, no shield, clock running)")
        return 0
    print("RESULT: RED -- spawn state still wrong (see S1/S2/S3)")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
